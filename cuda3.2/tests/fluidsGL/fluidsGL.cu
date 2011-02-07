/*
 * Copyright 1993-2010 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <GL/glew.h>
#include <cufft.h>
#include <cutil_inline.h>
#include <cutil_gl_inline.h>
#include <cuda_gl_interop.h>
#include "rendercheck_gl.h"

#ifdef WIN32
     bool IsOpenGLAvailable(const char *appName) { return true; }
#else
  #if (defined(__APPLE__) || defined(MACOSX))
     bool IsOpenGLAvailable(const char *appName) { return true; }
  #else
     // check if this is a linux machine
     #include <X11/Xlib.h>

     bool IsOpenGLAvailable(const char *appName)
     {
        Display *Xdisplay = XOpenDisplay(NULL);
        if (Xdisplay == NULL) {
           return false;
        } else {
           XCloseDisplay(Xdisplay);
           return true;
        }
     }
  #endif
#endif

#if defined(__APPLE__) || defined(MACOSX)
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include "fluidsGL_kernels.cu"

#define MAX_EPSILON_ERROR 1.0f

const char *sSDKname = "fluidsGL";

// Define the files that are to be save and the reference images for validation
const char *sOriginal[] =
{
    "fluidsGL.ppm",
    NULL
};

const char *sReference[] =
{
    "ref_fluidsGL.ppm",
    NULL
};

// CUDA example code that implements the frequency space version of 
// Jos Stam's paper 'Stable Fluids' in 2D. This application uses the 
// CUDA FFT library (CUFFT) to perform velocity diffusion and to 
// force non-divergence in the velocity field at each time step. It uses 
// CUDA-OpenGL interoperability to update the particle field directly
// instead of doing a copy to system memory before drawing. Texture is
// used for automatic bilinear interpolation at the velocity advection step. 

#ifdef __DEVICE_EMULATION__
#define DIM    64        // Square size of solver domain
#else
#define DIM    512       // Square size of solver domain
#endif
#define DS    (DIM*DIM)  // Total domain size
#define CPADW (DIM/2+1)  // Padded width for real->complex in-place FFT
#define RPADW (2*(DIM/2+1))  // Padded width for real->complex in-place FFT
#define PDS   (DIM*CPADW) // Padded total domain size

#define DT     0.09f     // Delta T for interative solver
#define VIS    0.0025f   // Viscosity constant
#define FORCE (5.8f*DIM) // Force scale factor 
#define FR     4         // Force update radius

#define TILEX 64 // Tile width
#define TILEY 64 // Tile height
#define TIDSX 64 // Tids in X
#define TIDSY 4  // Tids in Y

void cleanup(void);
void reshape(int x, int y);

// CUFFT plan handle
static cufftHandle planr2c;
static cufftHandle planc2r;
static cData *vxfield = NULL;
static cData *vyfield = NULL;

cData *hvfield = NULL;
cData *dvfield = NULL;
static int wWidth = max(512,DIM);
static int wHeight = max(512,DIM);

static int clicked = 0;
static int fpsCount = 0;
static int fpsLimit = 1;
unsigned int timer;

// Particle data
GLuint vbo = 0;                 // OpenGL vertex buffer object
struct cudaGraphicsResource *cuda_vbo_resource; // handles OpenGL-CUDA exchange
static cData *particles = NULL; // particle positions in host memory
static int lastx = 0, lasty = 0;

// Texture pitch
size_t tPitch = 0; // Now this is compatible with gcc in 64-bit

bool				  g_bQAReadback     = false;
bool				  g_bQAAddTestForce = true;
int					  g_iFrameToCompare = 100;
int                   g_TotalErrors     = 0;

// CheckFBO/BackBuffer class objects
CheckRender       *g_CheckRender = NULL;

void autoTest();


void addForces(cData *v, int dx, int dy, int spx, int spy, float fx, float fy, int r) { 

    dim3 tids(2*r+1, 2*r+1);
    
    addForces_k<<<1, tids>>>(v, dx, dy, spx, spy, fx, fy, r, tPitch);
    cutilCheckMsg("addForces_k failed.");
}

void advectVelocity(cData *v, float *vx, float *vy,
                    int dx, int pdx, int dy, float dt) { 
    
    dim3 grid((dx/TILEX)+(!(dx%TILEX)?0:1), (dy/TILEY)+(!(dy%TILEY)?0:1));

    dim3 tids(TIDSX, TIDSY);

    updateTexture(v, DIM*sizeof(cData), DIM, tPitch);
    advectVelocity_k<<<grid, tids>>>(v, vx, vy, dx, pdx, dy, dt, TILEY/TIDSY);

    cutilCheckMsg("advectVelocity_k failed.");
}

void diffuseProject(cData *vx, cData *vy, int dx, int dy, float dt,
                    float visc) { 
    // Forward FFT
    cufftExecR2C(planr2c, (cufftReal*)vx, (cufftComplex*)vx); 
    cufftExecR2C(planr2c, (cufftReal*)vy, (cufftComplex*)vy);

    uint3 grid = make_uint3((dx/TILEX)+(!(dx%TILEX)?0:1), 
                            (dy/TILEY)+(!(dy%TILEY)?0:1), 1);

    uint3 tids = make_uint3(TIDSX, TIDSY, 1);
    
    diffuseProject_k<<<grid, tids>>>(vx, vy, dx, dy, dt, visc, TILEY/TIDSY);
    cutilCheckMsg("diffuseProject_k failed.");

    // Inverse FFT
    cufftExecC2R(planc2r, (cufftComplex*)vx, (cufftReal*)vx); 
    cufftExecC2R(planc2r, (cufftComplex*)vy, (cufftReal*)vy);
}

void updateVelocity(cData *v, float *vx, float *vy, 
                    int dx, int pdx, int dy) { 

    dim3 grid((dx/TILEX)+(!(dx%TILEX)?0:1), (dy/TILEY)+(!(dy%TILEY)?0:1));

    dim3 tids(TIDSX, TIDSY);

    updateVelocity_k<<<grid, tids>>>(v, vx, vy, dx, pdx, dy, TILEY/TIDSY, tPitch);
    cutilCheckMsg("updateVelocity_k failed.");
}

void advectParticles(GLuint vbo, cData *v, int dx, int dy, float dt) {
    
    dim3 grid((dx/TILEX)+(!(dx%TILEX)?0:1), (dy/TILEY)+(!(dy%TILEY)?0:1));

    dim3 tids(TIDSX, TIDSY);

    cData *p;
    cutilSafeCall(cudaGraphicsMapResources(1, &cuda_vbo_resource, 0));
    size_t num_bytes; 
    cutilSafeCall(cudaGraphicsResourceGetMappedPointer((void **)&p, &num_bytes,  
						       cuda_vbo_resource));
    cutilCheckMsg("cudaGraphicsResourceGetMappedPointer failed");
   
    advectParticles_k<<<grid, tids>>>(p, v, dx, dy, dt, TILEY/TIDSY, tPitch);
    cutilCheckMsg("advectParticles_k failed.");
    
    cutilSafeCall(cudaGraphicsUnmapResources(1, &cuda_vbo_resource, 0));

    cutilCheckMsg("cudaGraphicsUnmapResources failed");
}

void simulateFluids(void)
{
   // simulate fluid
   advectVelocity(dvfield, (float*)vxfield, (float*)vyfield, DIM, RPADW, DIM, DT);
   diffuseProject(vxfield, vyfield, CPADW, DIM, DT, VIS);
   updateVelocity(dvfield, (float*)vxfield, (float*)vyfield, DIM, RPADW, DIM);
   advectParticles(vbo, dvfield, DIM, DIM, DT);
}

void display(void) {  

   if (!g_bQAReadback) {
       cutilCheckError(cutStartTimer(timer));  
       simulateFluids();
   }
   
   // render points from vertex buffer
   glClear(GL_COLOR_BUFFER_BIT);
   glColor4f(0,1,0,0.5f); glPointSize(1);
   glEnable(GL_POINT_SMOOTH);
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   glEnableClientState(GL_VERTEX_ARRAY);    
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_CULL_FACE); 
   glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbo);
   glVertexPointer(2, GL_FLOAT, 0, NULL);
   glDrawArrays(GL_POINTS, 0, DS);
   glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
   glDisableClientState(GL_VERTEX_ARRAY); 
   glDisableClientState(GL_TEXTURE_COORD_ARRAY); 
   glDisable(GL_TEXTURE_2D);

   if (g_bQAReadback) {
	   return;
   }

   // Finish timing before swap buffers to avoid refresh sync
   cutilCheckError(cutStopTimer(timer));
   glutSwapBuffers();
  
   fpsCount++;
   if (fpsCount == fpsLimit) {
       char fps[256];
       float ifps = 1.f / (cutGetAverageTimerValue(timer) / 1000.f);
       sprintf(fps, "Cuda/GL Stable Fluids (%d x %d): %3.1f fps", DIM, DIM, ifps);  
       glutSetWindowTitle(fps);
       fpsCount = 0; 
       fpsLimit = (int)max(ifps, 1.f);
       cutilCheckError(cutResetTimer(timer));  
    }

    glutPostRedisplay();
}

void autoTest(char **argv) 
{
    CFrameBufferObject *fbo = new CFrameBufferObject(wWidth, wHeight, 4, false, GL_TEXTURE_2D);
    g_CheckRender = new CheckFBO(wWidth, wHeight, 4, fbo);
    g_CheckRender->setPixelFormat(GL_RGBA);
    g_CheckRender->setExecPath(argv[0]);
    g_CheckRender->EnableQAReadback(true);
	
    fbo->bindRenderPath();

    reshape(wWidth, wHeight);

	for(int count=0;count<g_iFrameToCompare;count++)
	{
        simulateFluids();

		// add in a little force so the automated testing is interesing.
		if(g_bQAReadback && g_bQAAddTestForce) 
		{
			int x = wWidth/(count+1); int y = wHeight/(count+1);
			float fx = (x / (float)wWidth);        
			float fy = (y / (float)wHeight);
			int nx = (int)(fx * DIM);        
			int ny = (int)(fy * DIM);   

			int ddx = 35;
			int ddy = 35;
			fx = ddx / (float)wWidth;
			fy = ddy / (float)wHeight;
			int spy = ny-FR;
			int spx = nx-FR;

            addForces(dvfield, DIM, DIM, spx, spy, FORCE * DT * fx, FORCE * DT * fy, FR);
            lastx = x; lasty = y;
			//g_bQAAddTestForce = false; // only add it once
		}
    }

    display();

    fbo->unbindRenderPath();

	// compare to offical reference image, printing PASS or FAIL.
    printf("> (Frame %d) Readback BackBuffer\n", 100);
    g_CheckRender->readback( wWidth, wHeight );
    g_CheckRender->savePPM(sOriginal[0], true, NULL);
    if (!g_CheckRender->PPMvsPPM(sOriginal[0], sReference[0], MAX_EPSILON_ERROR, 0.25f)) {
        g_TotalErrors++;
    }
}

// very simple von neumann middle-square prng.  can't use rand() in -qatest
// mode because its implementation varies across platforms which makes testing
// for consistency in the important parts of this program difficult.
float myrand(void)
{
    static int seed = 72191;
    char sq[22];


    if (g_bQAReadback) {
        seed *= seed;
        sprintf(sq, "%010d", seed);
        // pull the middle 5 digits out of sq
        sq[8] = 0;
        seed = atoi(&sq[3]);

        return seed/99999.f;
    } else {
        return rand()/(float)RAND_MAX;
    }
}

void initParticles(cData *p, int dx, int dy) {
    int i, j;
    for (i = 0; i < dy; i++) {
        for (j = 0; j < dx; j++) {
            p[i*dx+j].x = (j+0.5f+(myrand() - 0.5f))/dx;
            p[i*dx+j].y = (i+0.5f+(myrand() - 0.5f))/dy;
        }
    }
}

void keyboard( unsigned char key, int x, int y) {
    switch( key) {
        case 27:
			exit (0);
			break;
        case 'r':
            memset(hvfield, 0, sizeof(cData) * DS);
            cudaMemcpy(dvfield, hvfield, sizeof(cData) * DS, 
                       cudaMemcpyHostToDevice);

            initParticles(particles, DIM, DIM);

	    cudaGraphicsUnregisterResource(cuda_vbo_resource);

            cutilCheckMsg("cudaGraphicsUnregisterBuffer failed");
    
            glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbo);
            glBufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof(cData) * DS, 
                            particles, GL_DYNAMIC_DRAW_ARB);
            glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);

	    cudaGraphicsGLRegisterBuffer(&cuda_vbo_resource, vbo, cudaGraphicsMapFlagsNone);
    
            cutilCheckMsg("cudaGraphicsGLRegisterBuffer failed");
            break;
        default: break;
    }
}

void click(int button, int updown, int x, int y) {
    lastx = x; lasty = y;
    clicked = !clicked;
}

void motion (int x, int y) {
    // Convert motion coordinates to domain
    float fx = (lastx / (float)wWidth);        
    float fy = (lasty / (float)wHeight);
    int nx = (int)(fx * DIM);        
    int ny = (int)(fy * DIM);   
    
    if (clicked && nx < DIM-FR && nx > FR-1 && ny < DIM-FR && ny > FR-1) {
        int ddx = x - lastx;
        int ddy = y - lasty;
        fx = ddx / (float)wWidth;
        fy = ddy / (float)wHeight;
        int spy = ny-FR;
        int spx = nx-FR;
        addForces(dvfield, DIM, DIM, spx, spy, FORCE * DT * fx, FORCE * DT * fy, FR);
        lastx = x; lasty = y;
    } 
    glutPostRedisplay();
}

void reshape(int x, int y) {
    wWidth = x; wHeight = y;
    glViewport(0, 0, x, y);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 1, 0, 0, 1); 
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glutPostRedisplay();
}

void cleanup(void) {
    cudaGraphicsUnregisterResource(cuda_vbo_resource);
    cutilCheckMsg("cudaGLUnregisterResource failed");

    unbindTexture();
    deleteTexture();

    // Free all host and device resources
    free(hvfield); free(particles); 
    cudaFree(dvfield); 
    cudaFree(vxfield); cudaFree(vyfield);
    cufftDestroy(planr2c);
    cufftDestroy(planc2r);

    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
    glDeleteBuffersARB(1, &vbo);
    
    cutilCheckError(cutDeleteTimer(timer));  
}

int initGL(int *argc, char **argv)
{
    if (IsOpenGLAvailable(sSDKname)) {
        fprintf( stderr, "   OpenGL device is Available\n");
    } else {
        fprintf( stderr, "   OpenGL device is NOT Available, [%s] exiting...\n  PASSED\n", sSDKname );
        return CUTFalse;
    }

    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
    glutInitWindowSize(wWidth, wHeight);
    glutCreateWindow("Compute Stable Fluids");
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutMouseFunc(click);
    glutMotionFunc(motion);
    glutReshapeFunc(reshape);


    glewInit();
    if (! glewIsSupported(
        "GL_ARB_vertex_buffer_object"
      )) 
    {
        fprintf( stderr, "ERROR: Support for necessary OpenGL extensions missing.");
        fflush( stderr);
        return CUTFalse;
    }
    return CUTTrue;
}


int main(int argc, char** argv) 
{
    int devID;
    cudaDeviceProp deviceProps;
    printf("[%s] - [OpenGL/CUDA simulation] starting...\n", sSDKname);

    // First initialize OpenGL context, so we can properly set the GL for CUDA.
    // This is necessary in order to achieve optimal performance with OpenGL/CUDA interop.
    if (CUTFalse == initGL(&argc, argv)) {
        cutilExit(argc, argv);
        exit(0);
    }

    // use command-line specified CUDA device, otherwise use device with highest Gflops/s
    if (cutCheckCmdLineFlag(argc, (const char**)argv, "device")) {
        devID = cutilGLDeviceInit(argc, argv);
        if (devID < 0) {
           printf("no device. exiting...\n");
           cutilExit(argc, argv);
           exit(0);
        }
    } else {
        devID = cutGetMaxGflopsDeviceId();
        cutilSafeCall(cudaGLSetGLDevice(devID));
    }

    // get number of SMs on this GPU
    cutilSafeCall(cudaGetDeviceProperties(&deviceProps, devID));
    printf("CUDA device [%s] has %d Multi-Processors\n", 
           deviceProps.name, deviceProps.multiProcessorCount);

	// automated build testing harness
    if (cutCheckCmdLineFlag(argc, (const char **)argv, "qatest") ||
        cutCheckCmdLineFlag(argc, (const char **)argv, "noprompt"))
    {
        g_bQAReadback = true;
    }
    
    // Allocate and initialize host data
    GLint bsize;

    cutilCheckError(cutCreateTimer(&timer));
    cutilCheckError(cutResetTimer(timer));  
    
    hvfield = (cData*)malloc(sizeof(cData) * DS);
    memset(hvfield, 0, sizeof(cData) * DS);
  
    // Allocate and initialize device data
    cudaMallocPitch((void**)&dvfield, &tPitch, sizeof(cData)*DIM, DIM);
    
    cudaMemcpy(dvfield, hvfield, sizeof(cData) * DS, 
               cudaMemcpyHostToDevice); 
    // Temporary complex velocity field data     
    cudaMalloc((void**)&vxfield, sizeof(cData) * PDS);
    cudaMalloc((void**)&vyfield, sizeof(cData) * PDS);
    
    setupTexture(DIM, DIM);
    bindTexture();
    
    // Create particle array
    particles = (cData*)malloc(sizeof(cData) * DS);
    memset(particles, 0, sizeof(cData) * DS);   
    
    initParticles(particles, DIM, DIM); 

    // Create CUFFT transform plan configuration
    cufftPlan2d(&planr2c, DIM, DIM, CUFFT_R2C);
    cufftPlan2d(&planc2r, DIM, DIM, CUFFT_C2R);
    // TODO: update kernels to use the new unpadded memory layout for perf
    // rather than the old FFTW-compatible layout
    cufftSetCompatibilityMode(planr2c, CUFFT_COMPATIBILITY_FFTW_PADDING);
    cufftSetCompatibilityMode(planc2r, CUFFT_COMPATIBILITY_FFTW_PADDING);

    glGenBuffersARB(1, &vbo);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbo);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof(cData) * DS, 
                    particles, GL_DYNAMIC_DRAW_ARB);

    glGetBufferParameterivARB(GL_ARRAY_BUFFER_ARB, GL_BUFFER_SIZE_ARB, &bsize); 
    if (bsize != (sizeof(cData) * DS))
        goto EXTERR;
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);

    cutilSafeCall(cudaGraphicsGLRegisterBuffer(&cuda_vbo_resource, vbo, cudaGraphicsMapFlagsNone));
    cutilCheckMsg("cudaGraphicsGLRegisterBuffer failed");

    if (g_bQAReadback)
    {
        autoTest(argv);

        printf("[fluidsGL] - Test Results: %d Failures\n", g_TotalErrors);
        printf((g_TotalErrors == 0) ? "PASSED" : "FAILED");
        printf("\n");

        cleanup();

    } else {
        atexit(cleanup); 
        glutMainLoop();
    }

    cudaThreadExit();
    cutilExit(argc, argv);
    return 0;

EXTERR:
    printf("Failed to initialize GL extensions.\n");

    cudaThreadExit();
    return 1;
}
