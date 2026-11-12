/* HUSH — Milestone 1 foundation probe.
 * Minimal GU application: clears the screen, renders a rotating shaded cube,
 * reads input, prints a heartbeat to stdout and exits cleanly on START or after
 * a fixed number of frames (so it can run unattended in PPSSPPHeadless). */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <stdio.h>

PSP_MODULE_INFO("HUSH_M1", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

static unsigned int __attribute__((aligned(16))) list[262144];

typedef struct { unsigned int color; float x, y, z; } Vtx;
static Vtx __attribute__((aligned(16))) cube[36];

static int running = 1;
static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; running = 0; return 0; }
static int cb_thread(SceSize a, void *b) {
    (void)a; (void)b;
    int id = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

static void build_cube(void) {
    static const float p[8][3] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    static const int f[6][4] = {{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0}};
    static const unsigned int col[6] = {0xff704030,0xff503020,0xff906040,0xff805030,0xffc09060,0xff302010};
    int n = 0;
    for (int i = 0; i < 6; i++) {
        int idx[6] = {0,1,2,0,2,3};
        for (int k = 0; k < 6; k++) {
            const float *v = p[f[i][idx[k]]];
            cube[n].color = col[i]; cube[n].x = v[0]; cube[n].y = v[1]; cube[n].z = v[2]; n++;
        }
    }
}

int main(void) {
    int th = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    build_cube();

    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, 512);
    sceGuDispBuffer(480, 272, (void *)0x88000, 512);
    sceGuDepthBuffer((void *)0x110000, 512);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, 480, 272);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    int frame = 0;
    SceCtrlData pad;
    while (running && frame < 600) {
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_START) break;
        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(0xff201010);
        sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
        sceGumMatrixMode(GU_PROJECTION); sceGumLoadIdentity();
        sceGumPerspective(60.0f, 480.0f / 272.0f, 0.5f, 100.0f);
        sceGumMatrixMode(GU_VIEW); sceGumLoadIdentity();
        sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();
        ScePspFVector3 pos = {0, 0, -5};
        ScePspFVector3 rot = {frame * 0.013f, frame * 0.021f + (pad.Lx - 128) * 0.01f, 0};
        sceGumTranslate(&pos); sceGumRotateXYZ(&rot);
        sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 36, 0, cube);
        sceGuFinish();
        sceGuSync(0, 0);
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
        if ((frame % 60) == 0) printf("HUSH_M1 frame %d buttons %08x lx %d\n", frame, pad.Buttons, pad.Lx);
        frame++;
    }
    printf("HUSH_M1 clean exit after %d frames\n", frame);
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
