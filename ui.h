// ============================================================
// CardputerGroovebox - ui.h
// ============================================================
#pragma once
#include "config.h"

extern Page    g_curPage;
extern bool    g_needRedraw;
extern float   g_holdProg;       // 0..1 long-press progress / mic level meter
extern char    g_holdLabel[16];  // label shown next to the footer bar
extern uint8_t g_soundParam;     // selected row on SOUND page
extern uint8_t g_songCursor;     // selected slot on SONG page
extern uint8_t g_streamSampleSlot;
extern uint8_t g_streamSampleMode;
extern uint8_t g_sampleEditMode;  // 0=browser, 1=slot sound, 2=step lock
extern uint8_t g_sampleParam;
extern uint8_t g_loopCursor;
extern uint8_t g_eventCursor;
extern uint8_t g_motionCursor;

// simple file browser (SAMPLE page)
#define BROWSER_MAX 64
extern char    g_fileList[BROWSER_MAX][SAMPLE_NAME_LEN];
extern uint8_t g_fileCount;
extern uint8_t g_fileSel;

void uiInit();
void uiDraw();
void uiSplash();
void uiStatus(const char* msg);          // transient message in footer
void uiScanSampleDir();                  // refresh SAMPLE page file list
