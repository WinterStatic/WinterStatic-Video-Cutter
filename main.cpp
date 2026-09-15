// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WinterStatic

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <objbase.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <iterator>
#include <memory>
#include <fstream>
#include <map>
#include <array>
#include <filesystem>
#include <system_error>
#include <functional>
#include <limits>

#include "subtitle_extract.h"

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Uuid.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")

// -----------------------------------------------------------------------------
// Minimal libmpv ABI declarations.
// The EXE loads libmpv-2.dll dynamically, so MSVC does not need an import lib.
// -----------------------------------------------------------------------------
struct mpv_handle;
struct mpv_node_list;
struct mpv_byte_array;

enum mpv_format {
    MPV_FORMAT_NONE = 0,
    MPV_FORMAT_STRING = 1,
    MPV_FORMAT_OSD_STRING = 2,
    MPV_FORMAT_FLAG = 3,
    MPV_FORMAT_INT64 = 4,
    MPV_FORMAT_DOUBLE = 5,
    MPV_FORMAT_NODE = 6,
    MPV_FORMAT_NODE_ARRAY = 7,
    MPV_FORMAT_NODE_MAP = 8,
    MPV_FORMAT_BYTE_ARRAY = 9
};

struct mpv_node {
    union {
        char* string;
        int flag;
        int64_t int64;
        double double_;
        mpv_node_list* list;
        mpv_byte_array* ba;
    } u{};
    mpv_format format = MPV_FORMAT_NONE;
};

struct mpv_node_list {
    int num = 0;
    mpv_node* values = nullptr;
    char** keys = nullptr;
};


enum mpv_event_id {
    MPV_EVENT_NONE = 0,
    MPV_EVENT_FILE_LOADED = 8
};

struct mpv_event {
    mpv_event_id event_id = MPV_EVENT_NONE;
    int error = 0;
    uint64_t reply_userdata = 0;
    void* data = nullptr;
};

namespace {

constexpr wchar_t kMainClass[] = L"MPVMPCNativeMain";
constexpr wchar_t kVideoClass[] = L"MPVMPCNativeVideo";
constexpr wchar_t kPanelClass[] = L"MPVMPCNativePanel";
constexpr wchar_t kMenuClass[] = L"MPVMPCNativeMenu";
constexpr wchar_t kSeekClass[] = L"MPVMPCNativeSeek";
constexpr wchar_t kButtonClass[] = L"MPVMPCNativeButton";
constexpr wchar_t kVolumeClass[] = L"MPVMPCNativeVolume";
constexpr wchar_t kProgressClass[] = L"MPVMPCNativeProgress";
constexpr wchar_t kOverlayClass[] = L"MPVMPCNativeOverlay";

constexpr wchar_t kAppTitle[] = L"WinterStatic Video Cutter";
constexpr wchar_t kVersionText[] = L"Version 0.2.2";
constexpr wchar_t kProjectUrl[] = L"https://github.com/WinterStatic";

constexpr int kMenuHeight = 24;
constexpr int kPanelBaseHeight = 96;
constexpr int kCutterContextBarHeight = 30;
constexpr int kSeekHeight = 17;
constexpr int kControlHeight = 26;
constexpr int kVolumeStep = 5;
constexpr int kDefaultVolume = 70;
constexpr int kSeekStepSeconds = 5;
constexpr int kSyncStepMs = 50;
constexpr int kRevealEdge = 8;
constexpr ULONGLONG kFullscreenCursorHideMs = 2000;
constexpr int kWindowedCursorWakeDistancePx = 4;
constexpr double kResumeMinSeconds = 5.0;
constexpr double kResumeEndMarginMaxSeconds = 10.0;

constexpr COLORREF C_BG = RGB(0, 0, 0);
constexpr COLORREF C_PANEL = RGB(27, 27, 27);
constexpr COLORREF C_PANEL_EDGE = RGB(38, 38, 38);
constexpr COLORREF C_BUTTON = RGB(35, 35, 35);
constexpr COLORREF C_BUTTON_DOWN = RGB(30, 34, 34);
constexpr COLORREF C_HOVER = RGB(138, 138, 138);
constexpr COLORREF C_TEXT = RGB(232, 232, 232);
constexpr COLORREF C_TEXT_DIM = RGB(175, 175, 175);
constexpr COLORREF C_AUTO_OFF_TEXT = RGB(105, 105, 105);
constexpr COLORREF C_SEEK_BG = RGB(22, 22, 22);
constexpr COLORREF C_SEEK_FILL = RGB(44, 44, 44);
constexpr COLORREF C_CHAPTER_MARKER = RGB(96, 96, 96);
constexpr COLORREF C_VOLUME_BG = RGB(18, 18, 18);
constexpr COLORREF C_VOLUME_FILL = RGB(112, 112, 112);
constexpr COLORREF C_BLACK = RGB(0, 0, 0);
constexpr COLORREF C_TEAL = RGB(0, 106, 109);
constexpr COLORREF C_MARK_A = RGB(54, 179, 92);
constexpr COLORREF C_MARK_A_BG = RGB(23, 83, 44);
constexpr COLORREF C_MARK_B = RGB(232, 176, 48);
constexpr COLORREF C_MARK_B_BG = RGB(103, 73, 14);
constexpr COLORREF C_PROGRESS_BG = RGB(19, 19, 19);
constexpr COLORREF C_PROGRESS_FILL = RGB(0, 106, 109);
constexpr COLORREF C_CROP_BG = RGB(31, 78, 121);
constexpr COLORREF C_NUKE_BG = RGB(112, 28, 28);
constexpr COLORREF C_CONVERT_BG = RGB(0, 84, 87);

constexpr UINT_PTR TIMER_SINGLE_CLICK = 1;
constexpr UINT_PTR TIMER_FULLSCREEN_HOVER = 2;
constexpr UINT_PTR TIMER_MPV_POLL = 3;
constexpr UINT_PTR TIMER_PLAYLIST_OSD_RESTORE = 4;
constexpr UINT_PTR TIMER_TASKBAR_ACTIVATE = 5;
constexpr int64_t kPlaylistOverlayId = 1;
constexpr UINT WM_APP_AUTO_NEXT_FILE = WM_APP + 1;
constexpr UINT WM_APP_FINAL_FULLSCREEN_REDRAW = WM_APP + 2;

constexpr ULONG_PTR kCopyDataOpenFiles = 0x57534F46; // "WSOF"
constexpr ULONG_PTR kCopyDataPausePlayback = 0x57535041; // "WSPA"
constexpr DWORD_PTR kCopyDataAck = 0x57534143; // "WSAC" - explicit protocol acknowledgment
constexpr UINT kTaskbarPlayPauseButtonId = 1;
constexpr UINT kTaskbarPreviousButtonId = 2;
constexpr UINT kTaskbarStopButtonId = 3;
constexpr UINT kTaskbarNextButtonId = 4;
constexpr UINT kTaskbarFullscreenButtonId = 5;
constexpr size_t kTaskbarButtonCount = 5;

constexpr int IDC_PLAY = 1001;
constexpr int IDC_STOP = 1002;
constexpr int IDC_PREV = 1003;
constexpr int IDC_SEEK_BACK = 1004;
constexpr int IDC_SEEK_FORWARD = 1005;
constexpr int IDC_NEXT = 1006;
constexpr int IDC_SPEED_DOWN = 1007;
constexpr int IDC_SPEED_UP = 1008;
constexpr int IDC_AUDIO = 1009;
constexpr int IDC_SUBS = 1010;
constexpr int IDC_MUTE = 1011;
constexpr int IDC_VOLUME = 1012;
constexpr int IDC_SEEK = 1013;
constexpr int IDC_STATUS = 1014;
constexpr int IDC_TIME = 1015;
constexpr int IDC_VOLUME_TEXT = 1016;
constexpr int IDC_AUDIO_INFO = 1017;
constexpr int IDC_SUB_INFO = 1018;
constexpr int IDC_AUTO_NEXT = 1019;
constexpr int IDC_PLAYLIST_OSD = 1020;
constexpr int IDC_PLAYLIST_MENU = 1021;
constexpr int IDC_LOOP_CURRENT = 1022;
constexpr int IDC_PLAYLIST_PREV_BUTTON = 1023;
constexpr int IDC_PLAYLIST_NEXT_BUTTON = 1024;

// Cutter row added below the original MPV controls.
constexpr int IDC_CUT_OPEN = 1050;
constexpr int IDC_CUT_MARK_A = 1051;
constexpr int IDC_CUT_MARK_B = 1052;
constexpr int IDC_CUT_CROP = 1053;
constexpr int IDC_CUT_FORMAT = 1054;
constexpr int IDC_CUT_RESOLUTION = 1055;
constexpr int IDC_CUT_SIZE = 1056;
constexpr int IDC_CUT_OUTPUT = 1057;
constexpr int IDC_CUT_CLEAR = 1058;
constexpr int IDC_CUT_NUKE = 1059;
constexpr int IDC_CUT_CONVERT = 1060;
constexpr int IDC_CUT_FORMAT_LABEL = 1061;
constexpr int IDC_CUT_RES_LABEL = 1062;
constexpr int IDC_CUT_SIZE_LABEL = 1063;
constexpr int IDC_CUT_OPTIONS = 1064;
constexpr int IDC_CUT_FRAME_PREV = 1065;
constexpr int IDC_CUT_FRAME_NEXT = 1066;
constexpr int IDC_CUT_AUDIO_OFFSET_LABEL = 1067;
constexpr int IDC_CUT_AUDIO_OFFSET_MINUS = 1068;
constexpr int IDC_CUT_AUDIO_OFFSET = 1069;
constexpr int IDC_CUT_AUDIO_OFFSET_PLUS = 1070;
constexpr int IDC_CUT_AUDIO_LABEL = 1071;
constexpr int IDC_CUT_AUDIO_MODE = 1072;
constexpr int IDC_CUT_AUDIO_KBPS = 1073;
constexpr int IDC_CUT_STICK_KEYFRAMES = 1074;
constexpr int IDC_CUT_MULTI = 1075;
constexpr int IDC_CUT_TWO_PASS = 1076;
constexpr int IDC_CUT_SUBTITLES = 1077;
constexpr int IDC_CUT_CRF_LABEL = 1078;
constexpr int IDC_CUT_CRF = 1079;
constexpr int IDC_CUT_SUB_SYNC = 1080;
constexpr int IDC_CUT_TRACKS = 1081;
constexpr int IDC_CUT_AUDIO_SYNC = 1082;
constexpr int IDC_AUDIO_SYNC_TRACK = 1083;
constexpr int IDC_AUDIO_SYNC_OFFSET_RESET = 1084;
constexpr int IDC_AUDIO_SYNC_STATUS = 1085;
constexpr int IDC_INPUT_LIST = 5100;
constexpr int IDC_INPUT_ADD = 5101;
constexpr int IDC_INPUT_REMOVE = 5102;
constexpr int IDC_INPUT_UP = 5103;
constexpr int IDC_INPUT_DOWN = 5104;
constexpr int IDC_INPUT_SORT = 5105;
constexpr int IDC_INPUT_CLEAR = 5106;
constexpr int IDC_AUDIO_TRACK_LIST = 5300;
constexpr int IDC_AUDIO_TRACK_ADD = 5301;
constexpr int IDC_AUDIO_TRACK_TOGGLE = 5302;
constexpr int IDC_AUDIO_TRACK_REMOVE = 5303;
constexpr int IDC_AUDIO_TRACK_UP = 5304;
constexpr int IDC_AUDIO_TRACK_DOWN = 5305;
constexpr int IDC_OPT_SHOW_POWERSHELL = 5004;
constexpr int IDC_OPT_CLOSE_POWERSHELL = 5005;
constexpr int IDC_OPT_OVERWRITE = 5006;
constexpr int IDC_SUB_ENABLE = 5200;
constexpr int IDC_SUB_PATH = 5201;
constexpr int IDC_SUB_BROWSE = 5202;
constexpr int IDC_SUB_CLEAR = 5203;
constexpr int IDC_SUB_SOFT = 5204;
constexpr int IDC_SUB_BURN = 5205;
constexpr int IDC_SUB_TITLE = 5206;
constexpr int IDC_SUB_KEEP_EMBEDDED = 5207;
constexpr int IDC_SUB_SOFT_LABEL = 5208;
constexpr int IDC_SUB_BURN_LABEL = 5209;
constexpr int IDC_SUB_OFFSET_LABEL = 5210;
constexpr int IDC_SUB_OFFSET_MINUS = 5211;
constexpr int IDC_SUB_OFFSET = 5212;
constexpr int IDC_SUB_OFFSET_PLUS = 5213;
constexpr int IDC_SUB_OFFSET_RESET = 5214;
constexpr int IDC_SUB_SYNC_A = 5215;
constexpr int IDC_SUB_SYNC_B = 5216;
constexpr int IDC_SUB_DRIFT = 5217;
constexpr int IDC_SUB_SYNC_CLEAR = 5218;
constexpr int IDC_SUB_SYNC_STATUS = 5219;
constexpr int IDC_SUB_SYNC_TRACK = 5220;
constexpr int IDC_SUB_TRACK_LIST = 5400;
constexpr int IDC_SUB_TRACK_ADD = 5401;
constexpr int IDC_SUB_TRACK_REMOVE = 5402;
constexpr int IDC_SUB_TRACK_UP = 5403;
constexpr int IDC_SUB_TRACK_DOWN = 5404;
constexpr int IDC_SUB_TRACK_TOGGLE = 5405;
constexpr int IDC_SUB_TRACK_SOFT = 5406;
constexpr int IDC_SUB_TRACK_BURN = 5407;
constexpr int IDC_SUB_TRACK_TITLE = 5408;

constexpr int IDM_FILE_OPEN = 2001;
constexpr int IDM_FILE_EXIT = 2002;
constexpr int IDM_VIEW_FULLSCREEN = 2003;
constexpr int IDM_PLAY_TOGGLE = 2004;
constexpr int IDM_PLAY_STOP = 2005;
constexpr int IDM_PLAY_PREV = 2006;
constexpr int IDM_PLAY_BACK = 2007;
constexpr int IDM_PLAY_FORWARD = 2008;
constexpr int IDM_PLAY_NEXT = 2009;
constexpr int IDM_SPEED_DOWN = 2010;
constexpr int IDM_SPEED_UP = 2011;
constexpr int IDM_AUDIO_TRACKS = 2012;
constexpr int IDM_SUB_TRACKS = 2013;
constexpr int IDM_MUTE = 2014;
constexpr int IDM_HELP_ABOUT = 2015;
constexpr int IDM_VIEW_OPTIONS = 2016;
constexpr int IDM_VIEW_MEDIA_INFO = 2017;
constexpr int IDM_VIEW_OPEN_MPV_CONFIG = 2018;
constexpr int IDM_AUTO_NEXT_FILE = 2019;
constexpr int IDM_PLAYLIST_SHOW = 2020;
constexpr int IDM_PLAYLIST_PREV = 2021;
constexpr int IDM_PLAYLIST_NEXT = 2022;
constexpr int IDM_LOOP_CURRENT_FILE = 2023;
constexpr int IDM_FILE_SCREENSHOT = 2024;
constexpr int IDM_VIEW_CUTTER_OPTIONS = 2025;

// Dedicated ranges for the one-shot right-click cascading track menus.
// These are intentionally far away from control/menu IDs.
constexpr int IDM_CTX_AUDIO_TRACK_BASE = 30000;
constexpr int IDM_CTX_SUB_TRACK_OFF = 31000;
constexpr int IDM_CTX_SUB_TRACK_BASE = 31001;
constexpr int IDM_CTX_PLAYLIST_BASE = 32000;
constexpr int kMaxPlaylistMenuItems = 1000;

constexpr int IDC_OPT_DELAY = 3001;
constexpr int IDC_OPT_OSD = 3002;
constexpr int IDC_OPT_AUDIO_LANG = 3003;
constexpr int IDC_OPT_SUB_LANG = 3004;
constexpr int IDC_OPT_OSD_SIZE = 3005;
constexpr int IDC_OPT_SUB_SIZE = 3006;
constexpr int IDC_OPT_PLAYLIST_START_OSD = 3007;
constexpr int IDC_OPT_GPU_API = 3008;
constexpr int IDC_OPT_SHORTCUTS = 3009;
constexpr int IDC_OPT_EXIT_FULLSCREEN_END = 3010;
constexpr int IDC_OPT_INSTANCE_BEHAVIOR = 3011;
constexpr int IDC_OPT_HIDE_WINDOWED_CURSOR = 3012;
constexpr int IDC_OPT_TASKBAR_BUTTONS = 3013;
constexpr int IDC_OPT_SCREENSHOT_FOLDER = 3014;
constexpr int IDC_OPT_SCREENSHOT_BROWSE = 3015;
constexpr int IDC_OPT_SHOW_STOP_BUTTON = 3016;
constexpr int IDC_OPT_SHOW_PLAYLIST_BUTTONS = 3017;

constexpr int IDC_SHORTCUT_PRIMARY_BASE = 4000;
constexpr int IDC_SHORTCUT_ALT_BASE = 4100;
constexpr int IDC_SHORTCUT_CLEAR = 4200;
constexpr int IDC_SHORTCUT_RESET = 4201;
constexpr int IDC_ABOUT_GITHUB = 4300;

HINSTANCE g_instance = nullptr;
HWND g_main = nullptr;
HWND g_video = nullptr;
HWND g_overlay = nullptr;
HWND g_cropOverlay = nullptr;
HWND g_menuBar = nullptr;
HWND g_panel = nullptr;
HWND g_seek = nullptr;
HWND g_play = nullptr;
HWND g_stop = nullptr;
HWND g_prev = nullptr;
HWND g_seekBack = nullptr;
HWND g_seekForward = nullptr;
HWND g_next = nullptr;
HWND g_speedDown = nullptr;
HWND g_speedUp = nullptr;
HWND g_autoNext = nullptr;
HWND g_loopCurrent = nullptr;
HWND g_playlistPrevButton = nullptr;
HWND g_playlistNextButton = nullptr;
HWND g_playlistOsd = nullptr;
HWND g_playlistMenu = nullptr;
HWND g_audio = nullptr;
HWND g_subs = nullptr;
HWND g_mute = nullptr;
HWND g_volume = nullptr;
HWND g_status = nullptr;
HWND g_time = nullptr;
HWND g_volumeText = nullptr;
HWND g_audioInfo = nullptr;
HWND g_subInfo = nullptr;

HWND g_cutOpen = nullptr;
HWND g_cutMulti = nullptr;
HWND g_cutTracks = nullptr;
HWND g_cutAudioSyncButton = nullptr;
HWND g_cutMarkAButton = nullptr;
HWND g_cutMarkBButton = nullptr;
HWND g_cutCropButton = nullptr;
HWND g_cutFormat = nullptr;
HWND g_cutResolution = nullptr;
HWND g_cutSize = nullptr;
HWND g_cutOutput = nullptr;
HWND g_cutClear = nullptr;
HWND g_cutNuke = nullptr;
HWND g_cutConvert = nullptr;
HWND g_cutFormatLabel = nullptr;
HWND g_cutResLabel = nullptr;
HWND g_cutSizeLabel = nullptr;
HWND g_cutOptions = nullptr;
HWND g_cutFramePrev = nullptr;
HWND g_cutFrameNext = nullptr;
HWND g_cutAudioOffsetLabel = nullptr;
HWND g_cutAudioOffsetMinus = nullptr;
HWND g_cutAudioOffset = nullptr;
HWND g_cutAudioOffsetPlus = nullptr;
HWND g_cutAudioOffsetReset = nullptr;
HWND g_cutAudioSyncTrackLabel = nullptr;
HWND g_cutAudioSyncTrack = nullptr;
HWND g_cutAudioSyncStatus = nullptr;
HWND g_cutAudioLabel = nullptr;
HWND g_cutAudioModeCombo = nullptr;
HWND g_cutAudioKbpsEdit = nullptr;
HWND g_cutStickKeyframesButton = nullptr;
HWND g_cutTwoPassButton = nullptr;
HWND g_cutSubtitles = nullptr;
HWND g_cutSubSyncButton = nullptr;
HWND g_cutSubEnable = nullptr;
HWND g_cutSubPathLabel = nullptr;
HWND g_cutSubPathEdit = nullptr;
HWND g_cutSubBrowse = nullptr;
HWND g_cutSubClear = nullptr;
HWND g_cutSubSoft = nullptr;
HWND g_cutSubBurn = nullptr;
HWND g_cutSubTitleLabel = nullptr;
HWND g_cutSubTitleEdit = nullptr;
HWND g_cutSubKeepEmbedded = nullptr;
HWND g_cutSubOffsetLabel = nullptr;
HWND g_cutSubOffsetMinus = nullptr;
HWND g_cutSubOffsetEdit = nullptr;
HWND g_cutSubOffsetPlus = nullptr;
HWND g_cutSubOffsetReset = nullptr;
HWND g_cutSubSyncA = nullptr;
HWND g_cutSubSyncB = nullptr;
HWND g_cutSubDrift = nullptr;
HWND g_cutSubSyncClear = nullptr;
HWND g_cutSubSyncStatus = nullptr;
HWND g_cutSubSyncTrackLabel = nullptr;
HWND g_cutSubSyncTrack = nullptr;
HWND g_cutCrfLabel = nullptr;
HWND g_cutCrfEdit = nullptr;
HWND g_cutProgress = nullptr;
HFONT g_font = nullptr;
HFONT g_smallFont = nullptr;
HBRUSH g_panelBrush = nullptr;
HBRUSH g_editBrush = nullptr;
HBRUSH g_seekBgBrush = nullptr;
HBRUSH g_seekFillBrush = nullptr;
HBRUSH g_chapterMarkerBrush = nullptr;
HBRUSH g_seekPlayheadBrush = nullptr;
HMENU g_fileMenu = nullptr;
HMENU g_viewMenu = nullptr;
HMENU g_playMenu = nullptr;
HMENU g_helpMenu = nullptr;
HHOOK g_menuHook = nullptr;
HICON g_taskbarPlayIcon = nullptr;
HICON g_taskbarPauseIcon = nullptr;
HICON g_taskbarPreviousIcon = nullptr;
HICON g_taskbarStopIcon = nullptr;
HICON g_taskbarNextIcon = nullptr;
HICON g_taskbarFullscreenIcon = nullptr;
ITaskbarList3* g_taskbarList = nullptr;
UINT g_taskbarButtonCreatedMsg = 0;
bool g_taskbarButtonAdded = false;
bool g_taskbarStateKnown = false;
bool g_taskbarLastIdle = true;
bool g_taskbarLastPaused = true;
bool g_taskbarLastPlaylistControls = false;
bool g_taskbarLastShowStopButton = false;

enum class CutterOutputMode {
    Source = 0,
    Mkv = 1,
    Mp4Copy = 2,
    Mp4H264 = 3,
    WebM = 4,
    ExtractAudio = 5,
    M4A = 6
};

enum class CutterAudioMode {
    Auto = 0,
    Copy = 1,
    None = 2
};

enum class CutterSubtitleMode {
    Soft = 0,
    Burn = 1
};

struct CutterManagedSubtitleTrack {
    bool embedded = true;
    int streamIndex = -1; // FFmpeg per-type subtitle stream index fallback.
    int64_t mpvTrackId = -1; // mpv track id, used only for player preview selection.
    int ffmpegStreamIndex = -1; // Exact global FFmpeg stream index from ffprobe/mpv reconciliation.
    std::wstring codec;
    bool syncCapable = false; // Embedded text subtitle with a verified FFmpeg stream identity.
    std::wstring externalPath;
    bool sourceBound = false; // true only for auto-paired sidecars tied to the current source.
    std::wstring language;
    std::wstring title;
    bool included = true;
    CutterSubtitleMode mode = CutterSubtitleMode::Soft;

    // Store timing state on each managed subtitle row so switching the
    // SUBS SYNC target cannot mix offsets or drift anchors between tracks.
    int offsetMs = 0;
    bool syncASet = false;
    bool syncBSet = false;
    double syncAVideoSeconds = 0.0;
    double syncBVideoSeconds = 0.0;
    int syncAOffsetMs = 0;
    int syncBOffsetMs = 0;
    bool driftEnabled = false;
};

struct CutterManagedAudioTrack {
    bool embedded = true;
    int streamIndex = -1; // FFmpeg per-type audio stream index for embedded tracks.
    int64_t mpvTrackId = -1; // mpv track id for player-side selection/preview.
    std::wstring externalPath;
    std::wstring language;
    std::wstring title;
    bool included = true;
    int offsetMs = 0; // Per-track Cutter timing offset. Positive delays this track.
    double durationSeconds = -1.0;
};

struct CutterVideoInfo {
    int w = 0;
    int h = 0;
    int dw = 0;
    int dh = 0;
    int rotate = 0;
    bool valid = false;
};

struct CutterCrop {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool valid = false;
};

CutterOutputMode g_cutMode = CutterOutputMode::Mp4H264;
std::wstring g_cutResolutionValue = L"Source";
double g_cutTargetKb = 0.0;
int g_cutAudioOffsetMs = 0;
int g_cutCrf = 32;
int g_cutAudioKbps = 0; // 0 = automatic mode-specific bitrate
CutterAudioMode g_cutAudioMode = CutterAudioMode::Auto;
bool g_cutShowPowerShell = true;
bool g_cutClosePowerShellWhenDone = true;
bool g_cutOverwriteEnabled = false;
bool g_cutStickKeyframes = false;
bool g_cutTwoPass = true;
bool g_cutSubtitlesEnabled = false;
std::wstring g_cutSubtitlePath;
CutterSubtitleMode g_cutSubtitleMode = CutterSubtitleMode::Soft;
std::wstring g_cutSubtitleTitle;
bool g_cutKeepEmbeddedSubtitles = false;
int g_cutSubtitleOffsetMs = 0;
bool g_cutSubtitleSyncASet = false;
bool g_cutSubtitleSyncBSet = false;
double g_cutSubtitleSyncAVideoSeconds = 0.0;
double g_cutSubtitleSyncBVideoSeconds = 0.0;
int g_cutSubtitleSyncAOffsetMs = 0;
int g_cutSubtitleSyncBOffsetMs = 0;
bool g_cutSubtitleDriftEnabled = false;
std::vector<std::wstring> g_cutSyncedSubtitlePaths;
bool g_cutSubtitleSyncOpen = false;
bool g_cutAudioSyncOpen = false;
std::vector<std::wstring> g_cutInputs;
std::wstring g_cutManagedAudioSourcePath;
bool g_cutAudioTracksInitialized = false;
std::vector<CutterManagedAudioTrack> g_cutAudioTracks;
int g_cutAudioSyncTrackIndex = -1;
double g_cutAudioPreviewBaseDelaySeconds = 0.0;
bool g_cutAudioPreviewSession = false;
std::wstring g_cutManagedSubtitleSourcePath;
bool g_cutSubtitleTracksInitialized = false;
std::vector<CutterManagedSubtitleTrack> g_cutSubtitleTracks;
int g_cutSubtitleSyncTrackIndex = -1;
std::wstring g_cutSubtitleProbeError;
std::wstring g_cutOutputPath;
double g_cutMarkA = -1.0;
double g_cutMarkB = -1.0;
bool g_cutMarkBAutoEnd = false;
CutterVideoInfo g_cutVideoInfo{};
CutterCrop g_cutCrop{};
enum class CutterCropDragMode {
    None = 0,
    NewRect,
    Move,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

bool g_cutCropActive = false;
bool g_cutCropSelecting = false;
bool g_cutCropDragging = false;
CutterCropDragMode g_cutCropDragMode = CutterCropDragMode::None;
POINT g_cutCropStart{};
POINT g_cutCropCurrent{};
POINT g_cutCropDragAnchor{};
RECT g_cutCropDragOriginRect{};
HANDLE g_cutWorkerJob = nullptr;
HANDLE g_cutWorkerProcess = nullptr;
DWORD g_cutWorkerPid = 0;
std::wstring g_cutWorkerScriptPath;
std::wstring g_cutConcatPath;
std::wstring g_cutProgressPath;
std::wstring g_cutProgressPhasePath;
double g_cutProgressDurationSeconds = 0.0;
std::wstring g_cutDonePath;
bool g_cutWorkerKeepConsoleOpen = false;
std::vector<HANDLE> g_cutLingeringJobs;
double g_cutProgressFraction = 0.0;
int g_cutProgressPasses = 1;
int g_cutProgressPulse = 0;
bool g_cutProgressIndeterminate = false;

enum class OsdPosition {
    GoldenCenter = 0,
    TopLeft = 1
};

enum class PlaylistStartOsd {
    Nothing = 0,
    Title = 1,
    Playlist = 2
};

enum class GpuApi {
    Auto = 0,
    D3D11 = 1,
    Vulkan = 2,
    OpenGL = 3
};

enum class InstanceOpenBehavior {
    OpenInExisting = 0,
    PauseExistingAndOpenNew = 1,
    JustOpenNew = 2
};

enum class ShortcutAction : size_t {
    OpenFile = 0,
    PlayPause,
    Stop,
    PreviousChapter,
    NextChapter,
    SeekBackward,
    SeekForward,
    SpeedDown,
    SpeedUp,
    VolumeUp,
    VolumeDown,
    Mute,
    ToggleFullscreen,
    ExitFullscreen,
    ToggleAutoNext,
    TogglePlaylist,
    PreviousPlaylist,
    NextPlaylist,
    CycleAudio,
    CycleSubtitle,
    AudioDelayDown,
    AudioDelayUp,
    ResetAudioDelay,
    SubtitleDelayDown,
    SubtitleDelayUp,
    ResetSubtitleDelay,
    MediaInfo,
    ToggleLoopCurrent,
    PreciseSeekBackward,
    PreciseSeekForward,
    TakeScreenshot,
    PreviousFrame,
    NextFrame,
    Count
};

struct KeyBinding {
    UINT vk = 0;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
};

struct ShortcutActionInfo {
    const wchar_t* label;
    const wchar_t* settingKey;
};

constexpr size_t kShortcutSlotCount = 2;
constexpr size_t kShortcutActionCount =
    static_cast<size_t>(ShortcutAction::Count);

const std::array<ShortcutActionInfo, kShortcutActionCount> kShortcutActionInfo{{
    { L"Open file", L"OpenFile" },
    { L"Play / Pause", L"PlayPause" },
    { L"Stop", L"Stop" },
    { L"Previous chapter", L"PreviousChapter" },
    { L"Next chapter", L"NextChapter" },
    { L"Seek backward 5 seconds", L"SeekBackward" },
    { L"Seek forward 5 seconds", L"SeekForward" },
    { L"Decrease speed", L"SpeedDown" },
    { L"Increase speed", L"SpeedUp" },
    { L"Volume up", L"VolumeUp" },
    { L"Volume down", L"VolumeDown" },
    { L"Mute", L"Mute" },
    { L"Toggle fullscreen", L"ToggleFullscreen" },
    { L"Exit fullscreen", L"ExitFullscreen" },
    { L"Toggle AUTO next file", L"ToggleAutoNext" },
    { L"Toggle playlist OSD", L"TogglePlaylist" },
    { L"Previous playlist item", L"PreviousPlaylist" },
    { L"Next playlist item", L"NextPlaylist" },
    { L"Cycle audio track", L"CycleAudio" },
    { L"Cycle subtitle track", L"CycleSubtitle" },
    { L"Audio delay -50 ms", L"AudioDelayDown" },
    { L"Audio delay +50 ms", L"AudioDelayUp" },
    { L"Reset audio delay", L"ResetAudioDelay" },
    { L"Subtitle delay -50 ms", L"SubtitleDelayDown" },
    { L"Subtitle delay +50 ms", L"SubtitleDelayUp" },
    { L"Reset subtitle delay", L"ResetSubtitleDelay" },
    { L"Media Info", L"MediaInfo" },
    { L"Toggle LOOP current file", L"ToggleLoopCurrent" },
    { L"Precise seek backward 5 seconds", L"PreciseSeekBackward" },
    { L"Precise seek forward 5 seconds", L"PreciseSeekForward" },
    { L"Take screenshot", L"TakeScreenshot" },
    { L"Previous frame", L"PreviousFrame" },
    { L"Next frame", L"NextFrame" }
}};

using ShortcutTable =
    std::array<std::array<KeyBinding, kShortcutSlotCount>, kShortcutActionCount>;

ShortcutTable g_shortcuts{};
ShortcutTable g_shortcutDialogBindings{};
WNDPROC g_shortcutEditOriginalProc = nullptr;
HWND g_lastShortcutEdit = nullptr;

enum class MediaShortcutSource {
    KeyDown,
    AppCommand
};

UINT g_lastHandledMediaVk = 0;
ULONGLONG g_lastHandledMediaTick = 0;
MediaShortcutSource g_lastHandledMediaSource = MediaShortcutSource::KeyDown;
constexpr ULONGLONG kMediaShortcutDedupMs = 80;

UINT g_singleClickDelayMs = 64;
int g_osdFontSize = 72;
int g_subtitleFontSize = 55;
OsdPosition g_osdPosition = OsdPosition::TopLeft;
PlaylistStartOsd g_playlistStartOsd = PlaylistStartOsd::Nothing;
GpuApi g_gpuApi = GpuApi::Auto;
InstanceOpenBehavior g_instanceOpenBehavior = InstanceOpenBehavior::OpenInExisting;
bool g_exitFullscreenOnPlaybackEnd = true;
bool g_hideWindowedCursor = true;
bool g_taskbarPlaylistControls = false;
bool g_showStopButton = false;
bool g_showPlaylistButtons = false;
bool g_loopCurrentFile = false;
// Empty means the normal Pictures\WinterStatic Video Cutter location.
std::wstring g_screenshotDirectory;
std::wstring g_preferredAudioLanguage;
std::wstring g_preferredSubtitleLanguage;

// These are updated only by deliberate user track changes (A/S or track menus).
// Automatic selection on file load never overwrites them.
bool g_haveRememberedAudio = false;
std::wstring g_rememberedAudioLanguage;
std::wstring g_rememberedAudioTitle;

bool g_haveRememberedSubtitle = false;
bool g_rememberedSubtitleOff = false;
std::wstring g_rememberedSubtitleLanguage;
std::wstring g_rememberedSubtitleTitle;

bool g_startMaximized = true;
bool g_haveSavedWindowRect = false;
bool g_mediaInfoVisible = false;
bool g_autoPlayNextFile = false;
bool g_lastEofReached = false;
bool g_seenPlayingSinceLoad = false;
bool g_suppressNextEofFullscreenExit = false;
bool g_fileLoadPending = false;
bool g_audioDelayTouched = false;
bool g_subtitleDelayTouched = false;
bool g_playlistOsdVisible = false;
bool g_haveLoadedPlaylistItem = false;
int64_t g_lastLoadedPlaylistPos = -1;
RECT g_savedWindowRect{};

bool g_fullscreen = false;
bool g_suppressFullscreenSizeLayout = false;
bool g_finalFullscreenRedrawPending = false;
bool g_controlsVisible = true;
bool g_pendingSingleClick = false;
bool g_singleClickApplied = false;
bool g_ignoreNextVideoButtonUp = false;
bool g_seekPointerDown = false;
bool g_seekDragging = false;
POINT g_seekMouseDownPoint{};
bool g_volumeDragging = false;
bool g_playing = false;
bool g_muted = false;
bool g_mpvReady = false;
bool g_initializingPlaybackEngine = true;
int g_volumePercent = kDefaultVolume;
int g_wheelRemainder = 0;
int g_menuHover = -1;
int g_activeTopMenu = -1;
int g_menuSwitchRequest = -1;
int g_pollCounter = 0;
double g_timePos = 0.0;
double g_duration = 0.0;
ULONGLONG g_lastControlsHover = 0;
ULONGLONG g_lastFullscreenMouseActivity = 0;
ULONGLONG g_lastWindowedVideoMouseActivity = 0;
bool g_fullscreenCursorHidden = false;
bool g_windowedCursorHidden = false;
bool g_cursorOverVideo = false;
bool g_trackingVideoMouseLeave = false;
POINT g_windowedVideoMouseAnchor{};
bool g_haveWindowedVideoMouseAnchor = false;
DWORD g_windowedStyle = 0;
DWORD g_windowedExStyle = 0;
RECT g_windowedRect{};
WINDOWPLACEMENT g_windowedPlacement{ sizeof(WINDOWPLACEMENT) };
std::wstring g_statusText = L"Stopped";
std::wstring g_audioInfoText = L"--";
std::wstring g_subInfoText = L"--";
std::wstring g_currentMediaPath;
std::wstring g_pendingAutoNextPath;
bool g_userStoppedPlayback = false;
int64_t g_userStoppedPlaylistPos = -1;
std::wstring g_skipResumeOnceForPath;
std::map<std::wstring, double> g_resumePositions;
bool g_resumePositionsDirty = false;
std::vector<double> g_chapterTimes;

// STICK KF uses a lazily built ffprobe keyframe index for adjacent-keyframe
// navigation. This avoids 5-second relative keyframe seeks jumping over
// intermediate keyframes when several fall inside the seek interval.
std::wstring g_keyframeIndexPath;
std::vector<double> g_keyframeTimes;
bool g_keyframeIndexAttempted = false;
bool g_keyframeIndexFailureShown = false;
std::wstring g_keyframeIndexError;
std::ptrdiff_t g_keyframeNavIndex = -1;

struct MpvApi {
    HMODULE dll = nullptr;
    mpv_handle* (__cdecl* create)() = nullptr;
    int (__cdecl* initialize)(mpv_handle*) = nullptr;
    void (__cdecl* terminate_destroy)(mpv_handle*) = nullptr;
    int (__cdecl* command)(mpv_handle*, const char* const[]) = nullptr;
    int (__cdecl* command_node)(mpv_handle*, mpv_node*, mpv_node*) = nullptr;
    int (__cdecl* set_option)(mpv_handle*, const char*, mpv_format, void*) = nullptr;
    int (__cdecl* set_option_string)(mpv_handle*, const char*, const char*) = nullptr;
    int (__cdecl* load_config_file)(mpv_handle*, const char*) = nullptr;
    int (__cdecl* get_property)(mpv_handle*, const char*, mpv_format, void*) = nullptr;
    int (__cdecl* set_property)(mpv_handle*, const char*, mpv_format, void*) = nullptr;
    int (__cdecl* set_property_string)(mpv_handle*, const char*, const char*) = nullptr;
    mpv_event* (__cdecl* wait_event)(mpv_handle*, double) = nullptr;
    void (__cdecl* free_fn)(void*) = nullptr;
    void (__cdecl* free_node_contents)(mpv_node*) = nullptr;
    const char* (__cdecl* error_string)(int) = nullptr;
} g_mpvApi;

mpv_handle* g_mpv = nullptr;
DLL_DIRECTORY_COOKIE g_libMpvDllDirectoryCookie = nullptr;

struct TrackInfo {
    int64_t id = 0;
    int ffIndex = -1; // mpv's FFmpeg global stream index when available.
    std::string type;
    std::string lang;
    std::string title;
    std::string codec;
    std::string externalFilename;
    bool selected = false;
};

struct PlaylistEntry {
    std::string filename;
    std::string title;
    bool current = false;
    bool playing = false;
};

const char* GpuApiOptionValue(GpuApi api) {
    switch (api) {
    case GpuApi::D3D11: return "d3d11";
    case GpuApi::Vulkan: return "vulkan";
    case GpuApi::OpenGL: return "opengl";
    case GpuApi::Auto:
    default: return "auto";
    }
}

void FillSolid(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    const int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) {
        const int fallback = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (fallback <= 0) return L"";
        std::wstring out(static_cast<size_t>(fallback), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), fallback);
        return out;
    }
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring GetExeDirectory() {
    wchar_t path[MAX_PATH * 4]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (n == 0 || n >= std::size(path)) return L".";
    std::wstring p(path, n);
    const size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}


bool DirectoryIsWritable(const std::wstring& directory) {
    const std::wstring testPath = directory + L"\\.winterstatic-cutter-write-test.tmp";
    HANDLE file = CreateFileW(
        testPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    DeleteFileW(testPath.c_str());
    return true;
}

std::wstring GetLocalSettingsPath() {
    wchar_t localAppData[MAX_PATH * 4]{};
    const DWORD n = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        localAppData,
        static_cast<DWORD>(std::size(localAppData)));

    std::wstring base;
    if (n > 0 && n < std::size(localAppData)) {
        base.assign(localAppData, n);
    } else {
        base = GetExeDirectory();
    }

    // Keep the legacy local settings directory for compatibility with existing
    // non-portable installs that could not write settings beside the executable.
    const std::wstring directory = base + L"\\MPV WinterStatic Edition";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory + L"\\settings.ini";
}

std::wstring GetSettingsPath() {
    static std::wstring path;
    if (!path.empty()) return path;

    const std::wstring exeDir = GetExeDirectory();
    path = DirectoryIsWritable(exeDir)
        ? exeDir + L"\\settings.ini"
        : GetLocalSettingsPath();

    return path;
}

std::wstring GetResumePath() {
    const std::filesystem::path settingsPath(GetSettingsPath());
    return (settingsPath.parent_path() / L"resume.ini").wstring();
}


std::wstring GetMpvConfigPath() {
    return GetExeDirectory() + L"\\mpv.conf";
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool EnsureMpvConfigExists() {
    const std::wstring path = GetMpvConfigPath();
    if (FileExists(path)) return true;

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) return false;

    const char initialText[] =
        "# WinterStatic Video Cutter - custom libmpv configuration\r\n"
        "# Add normal mpv options here, one per line.\r\n"
        "# Restart WinterStatic Video Cutter after saving changes.\r\n"
        "#\r\n"
        "# GPU API is controlled by Options and overrides gpu-api here.\r\n";

    DWORD written = 0;
    const BOOL writeOk = WriteFile(
        file,
        initialText,
        static_cast<DWORD>(sizeof(initialText) - 1),
        &written,
        nullptr);

    if (writeOk) FlushFileBuffers(file);
    CloseHandle(file);

    return writeOk && written == static_cast<DWORD>(sizeof(initialText) - 1);
}

void OpenMpvConfig() {
    if (!EnsureMpvConfigExists()) {
        MessageBoxW(
            g_main,
            L"Could not create mpv.conf beside the application executable.",
            kAppTitle,
            MB_OK | MB_ICONERROR);
        return;
    }

    const std::wstring path = GetMpvConfigPath();
    const std::wstring parameters = L"\"" + path + L"\"";
    const HINSTANCE result = ShellExecuteW(
        g_main,
        L"open",
        L"notepad.exe",
        parameters.c_str(),
        GetExeDirectory().c_str(),
        SW_SHOWNORMAL);

    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(
            g_main,
            L"Could not open mpv.conf in Notepad.",
            kAppTitle,
            MB_OK | MB_ICONERROR);
    }
}

using IniSection = std::map<std::string, std::string>;
using IniData = std::map<std::string, IniSection>;

std::string TrimIniText(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

IniData ReadIniDataFromPath(const std::wstring& path) {
    IniData data;
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return data;

    std::string section;
    std::string line;
    bool firstLine = true;

    while (std::getline(in, line)) {
        if (firstLine) {
            firstLine = false;
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
        }

        line = TrimIniText(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = TrimIniText(line.substr(1, line.size() - 2));
            continue;
        }

        const size_t equals = line.find('=');
        if (equals == std::string::npos || section.empty()) continue;

        const std::string key = TrimIniText(line.substr(0, equals));
        const std::string value = TrimIniText(line.substr(equals + 1));
        if (!key.empty()) data[section][key] = value;
    }

    return data;
}

IniData ReadIniData() {
    return ReadIniDataFromPath(GetSettingsPath());
}

bool WriteIniDataToPath(const IniData& data, const std::wstring& path) {
    std::string bytes;
    bool firstSection = true;

    for (const auto& sectionPair : data) {
        if (!firstSection) bytes += "\r\n";
        firstSection = false;

        bytes += "[";
        bytes += sectionPair.first;
        bytes += "]\r\n";

        for (const auto& keyValue : sectionPair.second) {
            bytes += keyValue.first;
            bytes += "=";
            bytes += keyValue.second;
            bytes += "\r\n";
        }
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    size_t offset = 0;

    while (offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<size_t>(bytes.size() - offset, 1024 * 1024));
        DWORD written = 0;

        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) ||
            written == 0) {
            ok = false;
            break;
        }

        offset += written;
    }

    if (ok && !FlushFileBuffers(file)) {
        ok = false;
    }

    CloseHandle(file);
    return ok;
}

bool WriteIniData(const IniData& data) {
    return WriteIniDataToPath(data, GetSettingsPath());
}

std::wstring ReadSettingString(const wchar_t* section, const wchar_t* key,
                               const wchar_t* fallback = L"") {
    const IniData data = ReadIniData();
    const std::string sectionUtf8 = WideToUtf8(section ? std::wstring(section) : L"");
    const std::string keyUtf8 = WideToUtf8(key ? std::wstring(key) : L"");

    const auto sectionIt = data.find(sectionUtf8);
    if (sectionIt == data.end()) return fallback ? fallback : L"";

    const auto valueIt = sectionIt->second.find(keyUtf8);
    if (valueIt == sectionIt->second.end()) return fallback ? fallback : L"";

    return Utf8ToWide(valueIt->second);
}

int ReadSettingInt(const wchar_t* section, const wchar_t* key, int fallback) {
    const std::wstring value = ReadSettingString(section, key, L"");
    if (value.empty()) return fallback;

    wchar_t* end = nullptr;
    const long parsed = wcstol(value.c_str(), &end, 10);
    if (!end || end == value.c_str()) return fallback;
    return static_cast<int>(parsed);
}

void WriteSettingString(const wchar_t* section, const wchar_t* key,
                        const std::wstring& value) {
    IniData data = ReadIniData();
    const std::string sectionUtf8 = WideToUtf8(section ? std::wstring(section) : L"");
    const std::string keyUtf8 = WideToUtf8(key ? std::wstring(key) : L"");
    data[sectionUtf8][keyUtf8] = WideToUtf8(value);
    WriteIniData(data);
}

void WriteSettingInt(const wchar_t* section, const wchar_t* key, int value) {
    WriteSettingString(section, key, std::to_wstring(value));
}

KeyBinding MakeKeyBinding(UINT vk, bool ctrl = false,
                          bool alt = false, bool shift = false) {
    KeyBinding binding{};
    binding.vk = vk;
    binding.ctrl = ctrl;
    binding.alt = alt;
    binding.shift = shift;
    return binding;
}

bool SameKeyBinding(const KeyBinding& a, const KeyBinding& b) {
    return a.vk == b.vk &&
           a.ctrl == b.ctrl &&
           a.alt == b.alt &&
           a.shift == b.shift;
}

ShortcutTable DefaultShortcutBindings() {
    ShortcutTable bindings{};

    auto set = [&](ShortcutAction action, size_t slot, const KeyBinding& binding) {
        bindings[static_cast<size_t>(action)][slot] = binding;
    };

    set(ShortcutAction::OpenFile, 0, MakeKeyBinding('O', true));
    set(ShortcutAction::PlayPause, 0, MakeKeyBinding(VK_SPACE));
    set(ShortcutAction::PlayPause, 1, MakeKeyBinding(VK_MEDIA_PLAY_PAUSE));
    set(ShortcutAction::PreviousChapter, 0, MakeKeyBinding(VK_LEFT, false, false, true));
    set(ShortcutAction::PreviousChapter, 1, MakeKeyBinding(VK_MEDIA_PREV_TRACK));
    set(ShortcutAction::NextChapter, 0, MakeKeyBinding(VK_RIGHT, false, false, true));
    set(ShortcutAction::NextChapter, 1, MakeKeyBinding(VK_MEDIA_NEXT_TRACK));
    set(ShortcutAction::SeekBackward, 0, MakeKeyBinding(VK_LEFT));
    set(ShortcutAction::SeekForward, 0, MakeKeyBinding(VK_RIGHT));
    set(ShortcutAction::VolumeUp, 0, MakeKeyBinding(VK_UP));
    set(ShortcutAction::VolumeDown, 0, MakeKeyBinding(VK_DOWN));
    set(ShortcutAction::Mute, 0, MakeKeyBinding('M'));
    set(ShortcutAction::ToggleFullscreen, 0, MakeKeyBinding('F'));
    set(ShortcutAction::ToggleFullscreen, 1, MakeKeyBinding(VK_RETURN));
    set(ShortcutAction::ExitFullscreen, 0, MakeKeyBinding(VK_ESCAPE));
    set(ShortcutAction::TogglePlaylist, 0, MakeKeyBinding('P'));
    set(ShortcutAction::PreviousPlaylist, 0, MakeKeyBinding(VK_PRIOR));
    set(ShortcutAction::PreviousPlaylist, 1, MakeKeyBinding(VK_UP, true));
    set(ShortcutAction::NextPlaylist, 0, MakeKeyBinding(VK_NEXT));
    set(ShortcutAction::NextPlaylist, 1, MakeKeyBinding(VK_DOWN, true));
    set(ShortcutAction::CycleAudio, 0, MakeKeyBinding('A'));
    set(ShortcutAction::CycleSubtitle, 0, MakeKeyBinding('S'));
    set(ShortcutAction::MediaInfo, 0, MakeKeyBinding('I'));
    set(ShortcutAction::TakeScreenshot, 0, MakeKeyBinding(VK_OEM_2));
    set(ShortcutAction::PreviousFrame, 0, MakeKeyBinding(VK_OEM_COMMA));
    set(ShortcutAction::NextFrame, 0, MakeKeyBinding(VK_OEM_PERIOD));

    return bindings;
}

int EncodeKeyBinding(const KeyBinding& binding) {
    if (binding.vk == 0) return 0;

    int encoded = static_cast<int>(binding.vk & 0xFFFFu);
    if (binding.ctrl) encoded |= (1 << 16);
    if (binding.alt) encoded |= (1 << 17);
    if (binding.shift) encoded |= (1 << 18);
    return encoded;
}

KeyBinding DecodeKeyBinding(int encoded) {
    if (encoded <= 0) return {};

    KeyBinding binding{};
    binding.vk = static_cast<UINT>(encoded & 0xFFFF);
    binding.ctrl = (encoded & (1 << 16)) != 0;
    binding.alt = (encoded & (1 << 17)) != 0;
    binding.shift = (encoded & (1 << 18)) != 0;
    return binding;
}

std::wstring ShortcutSettingName(size_t actionIndex, size_t slot) {
    std::wstring key = kShortcutActionInfo[actionIndex].settingKey;
    key += slot == 0 ? L"Primary" : L"Alternate";
    return key;
}

void LoadShortcutSettings() {
    g_shortcuts = DefaultShortcutBindings();

    const IniData data = ReadIniData();
    const auto sectionIt = data.find("Shortcuts");
    if (sectionIt == data.end()) return;

    for (size_t action = 0; action < kShortcutActionCount; ++action) {
        for (size_t slot = 0; slot < kShortcutSlotCount; ++slot) {
            const std::wstring keyWide = ShortcutSettingName(action, slot);
            const std::string key = WideToUtf8(keyWide);
            const auto valueIt = sectionIt->second.find(key);
            if (valueIt == sectionIt->second.end()) continue;

            try {
                const int encoded = std::stoi(valueIt->second);
                if (encoded >= 0) {
                    g_shortcuts[action][slot] = DecodeKeyBinding(encoded);
                }
            } catch (...) {
                // Keep the default for malformed hand-edited values.
            }
        }
    }

    // Migrate the older untouched Media Previous/Next-only chapter defaults
    // to the current primary bindings while preserving user customizations.
    const size_t prev = static_cast<size_t>(ShortcutAction::PreviousChapter);
    const size_t next = static_cast<size_t>(ShortcutAction::NextChapter);
    if (SameKeyBinding(g_shortcuts[prev][0], MakeKeyBinding(VK_MEDIA_PREV_TRACK)) &&
        g_shortcuts[prev][1].vk == 0) {
        g_shortcuts[prev][0] = MakeKeyBinding(VK_LEFT, true);
        g_shortcuts[prev][1] = MakeKeyBinding(VK_MEDIA_PREV_TRACK);
    }
    if (SameKeyBinding(g_shortcuts[next][0], MakeKeyBinding(VK_MEDIA_NEXT_TRACK)) &&
        g_shortcuts[next][1].vk == 0) {
        g_shortcuts[next][0] = MakeKeyBinding(VK_RIGHT, true);
        g_shortcuts[next][1] = MakeKeyBinding(VK_MEDIA_NEXT_TRACK);
    }

    // Migrate the older untouched Ctrl+Arrow chapter defaults to Shift+Arrow.
    // Media Previous/Next remain alternate chapter bindings, while custom
    // chapter shortcuts are preserved.
    if (SameKeyBinding(g_shortcuts[prev][0], MakeKeyBinding(VK_LEFT, true)) &&
        SameKeyBinding(g_shortcuts[prev][1], MakeKeyBinding(VK_MEDIA_PREV_TRACK))) {
        g_shortcuts[prev][0] = MakeKeyBinding(VK_LEFT, false, false, true);
    }
    if (SameKeyBinding(g_shortcuts[next][0], MakeKeyBinding(VK_RIGHT, true)) &&
        SameKeyBinding(g_shortcuts[next][1], MakeKeyBinding(VK_MEDIA_NEXT_TRACK))) {
        g_shortcuts[next][0] = MakeKeyBinding(VK_RIGHT, false, false, true);
    }

    // The comma/period frame-step defaults must not steal bindings that were
    // already assigned in an older settings.ini.
    const size_t previousFrame = static_cast<size_t>(ShortcutAction::PreviousFrame);
    const size_t nextFrame = static_cast<size_t>(ShortcutAction::NextFrame);
    auto bindingUsedOutside = [&](const KeyBinding& binding, size_t excludedAction) {
        for (size_t action = 0; action < kShortcutActionCount; ++action) {
            if (action == excludedAction) continue;
            for (size_t slot = 0; slot < kShortcutSlotCount; ++slot) {
                if (g_shortcuts[action][slot].vk != 0 &&
                    SameKeyBinding(g_shortcuts[action][slot], binding)) {
                    return true;
                }
            }
        }
        return false;
    };
    if (bindingUsedOutside(g_shortcuts[previousFrame][0], previousFrame)) {
        g_shortcuts[previousFrame][0] = {};
    }
    if (bindingUsedOutside(g_shortcuts[nextFrame][0], nextFrame)) {
        g_shortcuts[nextFrame][0] = {};
    }

    // Keep Media Play/Pause as the alternate default. Migrate only the exact
    // untouched Space + empty-alternate pair so custom bindings stay intact.
    const size_t playPause = static_cast<size_t>(ShortcutAction::PlayPause);
    const size_t volumeUp = static_cast<size_t>(ShortcutAction::VolumeUp);
    const size_t volumeDown = static_cast<size_t>(ShortcutAction::VolumeDown);
    if (SameKeyBinding(g_shortcuts[playPause][0], MakeKeyBinding(VK_SPACE)) &&
        g_shortcuts[playPause][1].vk == 0) {
        g_shortcuts[playPause][1] = MakeKeyBinding(VK_MEDIA_PLAY_PAUSE);
    }

    // Some older settings may contain temporary Windows volume-key alternates.
    // Those keys also change system volume, so remove only that exact temporary
    // default pair. Users can still assign media-volume keys manually.
    if (SameKeyBinding(g_shortcuts[volumeUp][0], MakeKeyBinding(VK_UP)) &&
        SameKeyBinding(g_shortcuts[volumeUp][1], MakeKeyBinding(VK_VOLUME_UP))) {
        g_shortcuts[volumeUp][1] = {};
    }
    if (SameKeyBinding(g_shortcuts[volumeDown][0], MakeKeyBinding(VK_DOWN)) &&
        SameKeyBinding(g_shortcuts[volumeDown][1], MakeKeyBinding(VK_VOLUME_DOWN))) {
        g_shortcuts[volumeDown][1] = {};
    }
}

void SaveShortcutSettings() {
    IniData data = ReadIniData();
    IniSection& section = data["Shortcuts"];

    for (size_t action = 0; action < kShortcutActionCount; ++action) {
        for (size_t slot = 0; slot < kShortcutSlotCount; ++slot) {
            const std::wstring keyWide = ShortcutSettingName(action, slot);
            section[WideToUtf8(keyWide)] =
                std::to_string(EncodeKeyBinding(g_shortcuts[action][slot]));
        }
    }

    WriteIniData(data);
}

std::wstring ShortcutKeyName(UINT vk) {
    if (vk >= 'A' && vk <= 'Z') {
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= '0' && vk <= '9') {
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return L"F" + std::to_wstring(vk - VK_F1 + 1);
    }

    switch (vk) {
    case VK_SPACE: return L"Space";
    case VK_RETURN: return L"Enter";
    case VK_ESCAPE: return L"Esc";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_PRIOR: return L"Page Up";
    case VK_NEXT: return L"Page Down";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_BACK: return L"Backspace";
    case VK_TAB: return L"Tab";
    case VK_MEDIA_PREV_TRACK: return L"Media Previous";
    case VK_MEDIA_NEXT_TRACK: return L"Media Next";
    case VK_MEDIA_PLAY_PAUSE: return L"Media Play/Pause";
    case VK_MEDIA_STOP: return L"Media Stop";
    case VK_VOLUME_MUTE: return L"Volume Mute";
    case VK_VOLUME_DOWN: return L"Volume Down";
    case VK_VOLUME_UP: return L"Volume Up";
    case VK_OEM_COMMA: return L",";
    case VK_OEM_PERIOD: return L".";
    case VK_OEM_2: return L"/";
    }

    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (scan != 0) {
        LONG keyData = static_cast<LONG>(scan << 16);
        switch (vk) {
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
            keyData |= (1 << 24);
            break;
        }
        wchar_t name[64]{};
        if (GetKeyNameTextW(keyData, name, static_cast<int>(std::size(name))) > 0) {
            return name;
        }
    }

    wchar_t fallback[24]{};
    swprintf_s(fallback, L"Key 0x%02X", static_cast<unsigned int>(vk));
    return fallback;
}

std::wstring KeyBindingText(const KeyBinding& binding) {
    if (binding.vk == 0) return L"None";

    std::wstring text;
    if (binding.ctrl) text += L"Ctrl+";
    if (binding.alt) text += L"Alt+";
    if (binding.shift) text += L"Shift+";
    text += ShortcutKeyName(binding.vk);
    return text;
}

std::wstring ShortcutSummary(ShortcutAction action) {
    const auto& pair = g_shortcuts[static_cast<size_t>(action)];
    std::wstring text;

    for (size_t slot = 0; slot < kShortcutSlotCount; ++slot) {
        if (pair[slot].vk == 0) continue;
        if (!text.empty()) text += L" / ";
        text += KeyBindingText(pair[slot]);
    }
    return text;
}

std::wstring ShortcutMenuLabel(const wchar_t* label, ShortcutAction action) {
    std::wstring text = label ? label : L"";
    const std::wstring shortcuts = ShortcutSummary(action);
    if (!shortcuts.empty()) {
        text += L"\t";
        text += shortcuts;
    }
    return text;
}

void LoadResumePositions() {
    g_resumePositions.clear();
    g_resumePositionsDirty = false;

    const IniData data = ReadIniDataFromPath(GetResumePath());
    for (const auto& sectionPair : data) {
        const auto pathIt = sectionPair.second.find("Path");
        const auto positionIt = sectionPair.second.find("PositionMs");
        if (pathIt == sectionPair.second.end() ||
            positionIt == sectionPair.second.end()) {
            continue;
        }

        const std::wstring path = Utf8ToWide(pathIt->second);
        if (path.empty()) continue;

        try {
            const long long positionMs = std::stoll(positionIt->second);
            if (positionMs >= 0) {
                g_resumePositions[path] =
                    static_cast<double>(positionMs) / 1000.0;
            }
        } catch (...) {
            // Ignore malformed entries rather than making resume state fatal.
        }
    }
}

bool FlushResumePositions() {
    if (!g_resumePositionsDirty) return true;

    IniData data;
    size_t index = 0;
    for (const auto& entry : g_resumePositions) {
        const std::string section = "File" + std::to_string(++index);
        data[section]["Path"] = WideToUtf8(entry.first);
        const long long positionMs =
            static_cast<long long>(std::llround(entry.second * 1000.0));
        data[section]["PositionMs"] = std::to_string(positionMs);
    }

    if (!WriteIniDataToPath(data, GetResumePath())) return false;
    g_resumePositionsDirty = false;
    return true;
}


bool RectTouchesAnyMonitor(const RECT& r) {
    return MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != nullptr;
}

void LoadSettings() {
    g_singleClickDelayMs = static_cast<UINT>(
        std::clamp(ReadSettingInt(L"General", L"ClickDelayMs", 64), 20, 500));
    g_volumePercent =
        std::clamp(ReadSettingInt(L"General", L"Volume", kDefaultVolume), 0, 100);
    g_muted = ReadSettingInt(L"General", L"Muted", 0) != 0;
    // Cutter keeps mpv playlist internals for multi-file input, but automatic
    // folder advance is intentionally disabled.
    g_autoPlayNextFile = false;
    g_exitFullscreenOnPlaybackEnd =
        ReadSettingInt(L"General", L"ExitFullscreenAtEnd", 1) != 0;
    g_hideWindowedCursor =
        ReadSettingInt(L"General", L"HideWindowedCursor", 1) != 0;
    // Use a distinct key because an older Full-controls setting represented
    // +/-5 second seek buttons. Do not reinterpret an existing seek preference
    // as previous/next playlist actions.
    g_taskbarPlaylistControls =
        ReadSettingInt(L"General", L"TaskbarPlaylistControls", 0) != 0;
    g_showStopButton =
        ReadSettingInt(L"General", L"ShowStopButton", 0) != 0;
    g_showPlaylistButtons =
        ReadSettingInt(L"General", L"ShowPlaylistButtons", 0) != 0;
    g_screenshotDirectory =
        ReadSettingString(L"Screenshots", L"SaveFolder", L"");
    const int instanceBehavior =
        std::clamp(ReadSettingInt(L"General", L"InstanceOpenBehavior", 0), 0, 2);
    g_instanceOpenBehavior =
        static_cast<InstanceOpenBehavior>(instanceBehavior);
    g_osdFontSize =
        std::clamp(ReadSettingInt(L"General", L"OsdFontSize", 72), 24, 180);
    g_subtitleFontSize =
        std::clamp(ReadSettingInt(L"Subtitles", L"FontSize", 55), 20, 120);

    const int osd = ReadSettingInt(L"General", L"OsdPosition", 1);
    g_osdPosition = osd == 1 ? OsdPosition::TopLeft : OsdPosition::GoldenCenter;

    const int playlistStartOsd =
        std::clamp(ReadSettingInt(L"General", L"PlaylistStartOsd", 0), 0, 2);
    g_playlistStartOsd = static_cast<PlaylistStartOsd>(playlistStartOsd);

    const int gpuApi =
        std::clamp(ReadSettingInt(L"Video", L"GpuApi", 0), 0, 3);
    g_gpuApi = static_cast<GpuApi>(gpuApi);

    LoadShortcutSettings();

    // Preferred languages are optional fallbacks. A remembered manual
    // selection takes priority when a matching track exists.
    g_preferredAudioLanguage =
        ReadSettingString(L"Tracks", L"AudioLanguage", L"");
    g_preferredSubtitleLanguage =
        ReadSettingString(L"Tracks", L"SubtitleLanguage", L"");

    g_haveRememberedAudio =
        ReadSettingInt(L"Tracks", L"LastAudioValid", 0) != 0;
    g_rememberedAudioLanguage =
        ReadSettingString(L"Tracks", L"LastAudioLanguage", L"");
    g_rememberedAudioTitle =
        ReadSettingString(L"Tracks", L"LastAudioTitle", L"");

    g_haveRememberedSubtitle =
        ReadSettingInt(L"Tracks", L"LastSubtitleValid", 0) != 0;
    g_rememberedSubtitleOff =
        ReadSettingInt(L"Tracks", L"LastSubtitleOff", 0) != 0;
    g_rememberedSubtitleLanguage =
        ReadSettingString(L"Tracks", L"LastSubtitleLanguage", L"");
    g_rememberedSubtitleTitle =
        ReadSettingString(L"Tracks", L"LastSubtitleTitle", L"");

    g_startMaximized = ReadSettingInt(L"Window", L"Maximized", 1) != 0;

    const int x = ReadSettingInt(L"Window", L"X", INT_MIN);
    const int y = ReadSettingInt(L"Window", L"Y", INT_MIN);
    const int w = ReadSettingInt(L"Window", L"Width", 0);
    const int h = ReadSettingInt(L"Window", L"Height", 0);

    if (x != INT_MIN && y != INT_MIN && w >= 650 && h >= 400) {
        RECT r{ x, y, x + w, y + h };
        if (RectTouchesAnyMonitor(r)) {
            g_savedWindowRect = r;
            g_haveSavedWindowRect = true;
        }
    }

    g_cutMode = static_cast<CutterOutputMode>(
        std::clamp(ReadSettingInt(L"Cutter", L"Format", 3), 0, 6));
    g_cutResolutionValue = ReadSettingString(L"Cutter", L"Resolution", L"Source");
    if (g_cutResolutionValue.empty()) g_cutResolutionValue = L"Source";
    std::wstring targetText = ReadSettingString(L"Cutter", L"TargetKB", L"");
    bool migratedTargetMb = false;
    if (targetText.empty()) {
        targetText = ReadSettingString(L"Cutter", L"TargetMB", L"");
        migratedTargetMb = !targetText.empty();
    }
    if (targetText.empty()) {
        g_cutTargetKb = 0.0;
    } else {
        wchar_t* targetEnd = nullptr;
        const double parsedTarget = wcstod(targetText.c_str(), &targetEnd);
        if (targetEnd && targetEnd != targetText.c_str() && std::isfinite(parsedTarget)) {
            const double kb = migratedTargetMb ? parsedTarget * 1024.0 : parsedTarget;
            g_cutTargetKb = std::clamp(kb, 0.0, 100000000.0);
        }
    }
    g_cutAudioOffsetMs = std::clamp(
        ReadSettingInt(L"Cutter", L"AudioOffsetMs", 0), -600000, 600000);
    g_cutCrf = std::clamp(ReadSettingInt(L"Cutter", L"CRF", 32), 0, 51);
    {
        std::wstring audioKbpsOverride = ReadSettingString(L"Cutter", L"AudioKbpsOverride", L"");
        if (audioKbpsOverride.empty()) {
            g_cutAudioKbps = 0;
        } else {
            wchar_t* audioEnd = nullptr;
            const long parsed = wcstol(audioKbpsOverride.c_str(), &audioEnd, 10);
            g_cutAudioKbps = (audioEnd && *audioEnd == L'\0' && parsed >= 16 && parsed <= 512)
                ? static_cast<int>(parsed) : 0;
        }
    }
    g_cutAudioMode = static_cast<CutterAudioMode>(
        std::clamp(ReadSettingInt(L"Cutter", L"AudioMode", 0), 0, 2));
    g_cutShowPowerShell = ReadSettingInt(L"Cutter", L"ShowPowerShell", 1) != 0;
    g_cutClosePowerShellWhenDone = ReadSettingInt(L"Cutter", L"ClosePowerShellWhenDone", 1) != 0;
    g_cutOverwriteEnabled = ReadSettingInt(L"Cutter", L"Overwrite", 0) != 0;
    g_cutStickKeyframes = ReadSettingInt(L"Cutter", L"StickKeyframes", 0) != 0;
    g_cutTwoPass = ReadSettingInt(L"Cutter", L"TwoPass", 1) != 0;
    g_cutSubtitlesEnabled = ReadSettingInt(L"Cutter", L"SubtitlesEnabled", 0) != 0;
    g_cutSubtitlePath = ReadSettingString(L"Cutter", L"SubtitlePath", L"");
    g_cutSubtitleMode = static_cast<CutterSubtitleMode>(
        std::clamp(ReadSettingInt(L"Cutter", L"SubtitleMode", 0), 0, 1));
    g_cutSubtitleTitle = ReadSettingString(L"Cutter", L"SubtitleTitle", L"");
    // The legacy blanket KeepEmbeddedSubtitles setting is no longer exposed in
    // SUBS OPT. Managed embedded subtitle rows now default OUT for clip jobs.
    g_cutKeepEmbeddedSubtitles = false;
    g_cutSubtitleOffsetMs = std::clamp(
        ReadSettingInt(L"Cutter", L"SubtitleOffsetMs", 0), -600000, 600000);

}

void SaveAudioState() {
    WriteSettingInt(L"General", L"Volume", g_volumePercent);
    WriteSettingInt(L"General", L"Muted", g_muted ? 1 : 0);
}

std::wstring AbsoluteLocalPath(const std::wstring& path);

void SaveSettings() {
    // Batch settings persistence into one INI read/write cycle. Re-reading,
    // rewriting and flushing the entire INI for every individual setting made
    // hot Cutter controls such as Format and Resolution visibly stall.
    IniData data = ReadIniData();

    const auto putString = [&data](const wchar_t* section, const wchar_t* key,
                                   const std::wstring& value) {
        const std::string sectionUtf8 = WideToUtf8(section ? std::wstring(section) : L"");
        const std::string keyUtf8 = WideToUtf8(key ? std::wstring(key) : L"");
        data[sectionUtf8][keyUtf8] = WideToUtf8(value);
    };
    const auto putInt = [&putString](const wchar_t* section, const wchar_t* key, int value) {
        putString(section, key, std::to_wstring(value));
    };

    putInt(L"General", L"ClickDelayMs", static_cast<int>(g_singleClickDelayMs));
    putInt(L"General", L"Volume", g_volumePercent);
    putInt(L"General", L"Muted", g_muted ? 1 : 0);
    putInt(L"General", L"AutoPlayNextFile", 0);
    putInt(L"General", L"ExitFullscreenAtEnd", g_exitFullscreenOnPlaybackEnd ? 1 : 0);
    putInt(L"General", L"HideWindowedCursor", g_hideWindowedCursor ? 1 : 0);
    putInt(L"General", L"TaskbarPlaylistControls", g_taskbarPlaylistControls ? 1 : 0);
    putInt(L"General", L"ShowStopButton", g_showStopButton ? 1 : 0);
    putInt(L"General", L"ShowPlaylistButtons", g_showPlaylistButtons ? 1 : 0);
    putString(L"Screenshots", L"SaveFolder", g_screenshotDirectory);
    putInt(L"General", L"InstanceOpenBehavior", static_cast<int>(g_instanceOpenBehavior));
    putInt(L"General", L"OsdFontSize", g_osdFontSize);
    putInt(L"Subtitles", L"FontSize", g_subtitleFontSize);
    putInt(L"General", L"OsdPosition", g_osdPosition == OsdPosition::TopLeft ? 1 : 0);
    putInt(L"General", L"PlaylistStartOsd", static_cast<int>(g_playlistStartOsd));
    putInt(L"Video", L"GpuApi", static_cast<int>(g_gpuApi));
    putString(L"Tracks", L"AudioLanguage", g_preferredAudioLanguage);
    putString(L"Tracks", L"SubtitleLanguage", g_preferredSubtitleLanguage);

    putInt(L"Tracks", L"LastAudioValid", g_haveRememberedAudio ? 1 : 0);
    putString(L"Tracks", L"LastAudioLanguage", g_rememberedAudioLanguage);
    putString(L"Tracks", L"LastAudioTitle", g_rememberedAudioTitle);
    putInt(L"Tracks", L"LastSubtitleValid", g_haveRememberedSubtitle ? 1 : 0);
    putInt(L"Tracks", L"LastSubtitleOff", g_rememberedSubtitleOff ? 1 : 0);
    putString(L"Tracks", L"LastSubtitleLanguage", g_rememberedSubtitleLanguage);
    putString(L"Tracks", L"LastSubtitleTitle", g_rememberedSubtitleTitle);

    putInt(L"Cutter", L"Format", static_cast<int>(g_cutMode));
    putString(L"Cutter", L"Resolution", g_cutResolutionValue);
    if (g_cutTargetKb > 0.0) {
        wchar_t cutterTarget[64]{};
        swprintf_s(cutterTarget, L"%.0f", g_cutTargetKb);
        putString(L"Cutter", L"TargetKB", cutterTarget);
    } else {
        putString(L"Cutter", L"TargetKB", L"");
    }
    putString(L"Cutter", L"TargetMB", L"");
    putInt(L"Cutter", L"AudioOffsetMs", g_cutAudioOffsetMs);
    putInt(L"Cutter", L"CRF", g_cutCrf);
    putString(L"Cutter", L"AudioKbpsOverride",
              g_cutAudioKbps > 0 ? std::to_wstring(g_cutAudioKbps) : L"");
    putString(L"Cutter", L"AudioKbps", L"");
    putInt(L"Cutter", L"AudioMode", static_cast<int>(g_cutAudioMode));
    putInt(L"Cutter", L"ShowPowerShell", g_cutShowPowerShell ? 1 : 0);
    putInt(L"Cutter", L"ClosePowerShellWhenDone", g_cutClosePowerShellWhenDone ? 1 : 0);
    putInt(L"Cutter", L"Overwrite", g_cutOverwriteEnabled ? 1 : 0);
    putInt(L"Cutter", L"StickKeyframes", g_cutStickKeyframes ? 1 : 0);
    putInt(L"Cutter", L"TwoPass", g_cutTwoPass ? 1 : 0);
    putInt(L"Cutter", L"SubtitlesEnabled", g_cutSubtitlesEnabled ? 1 : 0);
    putString(L"Cutter", L"SubtitlePath", g_cutSubtitlePath);
    putInt(L"Cutter", L"SubtitleMode", static_cast<int>(g_cutSubtitleMode));
    putString(L"Cutter", L"SubtitleTitle", g_cutSubtitleTitle);
    putInt(L"Cutter", L"KeepEmbeddedSubtitles", g_cutKeepEmbeddedSubtitles ? 1 : 0);
    int persistedSubtitleOffsetMs = g_cutSubtitleOffsetMs;
    if (g_cutSubtitleTracksInitialized &&
        g_cutSubtitleSyncTrackIndex >= 0 &&
        g_cutSubtitleSyncTrackIndex < static_cast<int>(g_cutSubtitleTracks.size()) &&
        g_cutSubtitleTracks[static_cast<size_t>(g_cutSubtitleSyncTrackIndex)].embedded) {
        // Embedded timing state is per-session/per-job. Do not accidentally
        // persist its offset against the remembered legacy external subtitle.
        persistedSubtitleOffsetMs = 0;
        if (!g_cutSubtitlePath.empty()) {
            const std::wstring remembered = AbsoluteLocalPath(g_cutSubtitlePath);
            for (const auto& track : g_cutSubtitleTracks) {
                if (!track.embedded &&
                    _wcsicmp(AbsoluteLocalPath(track.externalPath).c_str(),
                             remembered.c_str()) == 0) {
                    persistedSubtitleOffsetMs = track.offsetMs;
                    break;
                }
            }
        }
    }
    putInt(L"Cutter", L"SubtitleOffsetMs", persistedSubtitleOffsetMs);

    WINDOWPLACEMENT wp{ sizeof(WINDOWPLACEMENT) };
    if (g_fullscreen && g_windowedPlacement.length == sizeof(WINDOWPLACEMENT)) {
        wp = g_windowedPlacement;
    } else if (g_main) {
        GetWindowPlacement(g_main, &wp);
    }

    const bool maximized =
        wp.showCmd == SW_SHOWMAXIMIZED ||
        (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & WPF_RESTORETOMAXIMIZED));
    putInt(L"Window", L"Maximized", maximized ? 1 : 0);

    const RECT r = wp.rcNormalPosition;
    const int width = static_cast<int>(r.right - r.left);
    const int height = static_cast<int>(r.bottom - r.top);
    if (width >= 650 && height >= 400) {
        putInt(L"Window", L"X", static_cast<int>(r.left));
        putInt(L"Window", L"Y", static_cast<int>(r.top));
        putInt(L"Window", L"Width", width);
        putInt(L"Window", L"Height", height);
    }

    WriteIniData(data);
}

std::wstring FormatTime(double seconds) {
    if (!(seconds >= 0.0)) seconds = 0.0;
    const int whole = static_cast<int>(seconds + 0.5);
    const int hours = whole / 3600;
    const int minutes = (whole % 3600) / 60;
    const int secs = whole % 60;
    wchar_t buf[32]{};
    if (hours > 0) swprintf_s(buf, L"%d:%02d:%02d", hours, minutes, secs);
    else swprintf_s(buf, L"%02d:%02d", minutes, secs);
    return buf;
}

std::string UpperAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}

void EnableDarkModeForApp() {
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    enum class PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, Max };
    using SetPreferredAppModeFn = PreferredAppMode (WINAPI*)(PreferredAppMode);
    using FlushMenuThemesFn = void (WINAPI*)();
    auto setPreferred = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
    auto flushMenus = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
    if (setPreferred) setPreferred(PreferredAppMode::ForceDark);
    if (flushMenus) flushMenus();
    FreeLibrary(ux);
}

void ApplyDarkTheme(HWND hwnd) {
    if (!hwnd) return;
    BOOL enabled = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &enabled, sizeof(enabled)))) {
        DwmSetWindowAttribute(hwnd, 19, &enabled, sizeof(enabled));
    }
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (ux) {
        using AllowDarkModeForWindowFn = BOOL (WINAPI*)(HWND, BOOL);
        auto allowDark = reinterpret_cast<AllowDarkModeForWindowFn>(GetProcAddress(ux, MAKEINTRESOURCEA(133)));
        if (allowDark) allowDark(hwnd, TRUE);
        FreeLibrary(ux);
    }
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}

void ApplyFont(HWND hwnd, bool useSmallFont = false) {
    if (hwnd) {
        HFONT font = useSmallFont ? g_smallFont : g_font;
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

template <typename T>
bool LoadProc(T& target, const char* name) {
    target = reinterpret_cast<T>(GetProcAddress(g_mpvApi.dll, name));
    return target != nullptr;
}

std::wstring MpvError(int code) {
    if (g_mpvApi.error_string) {
        const char* s = g_mpvApi.error_string(code);
        if (s) return Utf8ToWide(s);
    }
    wchar_t buf[64]{};
    swprintf_s(buf, L"libmpv error %d", code);
    return buf;
}

bool MpvGetFlag(const char* name, bool fallback = false) {
    if (!g_mpvReady || !g_mpvApi.get_property) return fallback;
    int value = fallback ? 1 : 0;
    if (g_mpvApi.get_property(g_mpv, name, MPV_FORMAT_FLAG, &value) < 0) return fallback;
    return value != 0;
}

double MpvGetDouble(const char* name, double fallback = 0.0) {
    if (!g_mpvReady || !g_mpvApi.get_property) return fallback;
    double value = fallback;
    if (g_mpvApi.get_property(g_mpv, name, MPV_FORMAT_DOUBLE, &value) < 0) return fallback;
    return value;
}

int64_t MpvGetInt64(const char* name, int64_t fallback = 0) {
    if (!g_mpvReady || !g_mpvApi.get_property) return fallback;
    int64_t value = fallback;
    if (g_mpvApi.get_property(g_mpv, name, MPV_FORMAT_INT64, &value) < 0) return fallback;
    return value;
}

std::string MpvGetString(const char* name) {
    if (!g_mpvReady || !g_mpvApi.get_property || !g_mpvApi.free_fn) return {};
    char* value = nullptr;
    if (g_mpvApi.get_property(g_mpv, name, MPV_FORMAT_STRING, &value) < 0 || !value) return {};
    std::string result(value);
    g_mpvApi.free_fn(value);
    return result;
}

bool MpvSetFlag(const char* name, bool value) {
    if (!g_mpvReady) return false;
    int v = value ? 1 : 0;
    return g_mpvApi.set_property(g_mpv, name, MPV_FORMAT_FLAG, &v) >= 0;
}

bool MpvSetDouble(const char* name, double value) {
    if (!g_mpvReady) return false;
    return g_mpvApi.set_property(g_mpv, name, MPV_FORMAT_DOUBLE, &value) >= 0;
}

bool ResumePositionShouldBeKept(double position, double duration) {
    if (!std::isfinite(position) || position < kResumeMinSeconds) return false;
    if (duration <= 0.0 || !std::isfinite(duration)) return true;

    const double endMargin = std::min(
        kResumeEndMarginMaxSeconds,
        std::max(2.0, duration * 0.05));
    return position < duration - endMargin;
}

void SaveCurrentResumePosition() {
    if (g_currentMediaPath.empty()) return;

    bool changed = false;
    const bool keep = ResumePositionShouldBeKept(g_timePos, g_duration);
    const auto existing = g_resumePositions.find(g_currentMediaPath);

    if (keep) {
        if (existing == g_resumePositions.end() ||
            std::abs(existing->second - g_timePos) >= 0.05) {
            g_resumePositions[g_currentMediaPath] = g_timePos;
            changed = true;
        }
    } else if (existing != g_resumePositions.end()) {
        g_resumePositions.erase(existing);
        changed = true;
    }

    if (changed) {
        g_resumePositionsDirty = true;
        FlushResumePositions();
    }
}

void RestoreCurrentResumePosition() {
    if (!g_mpvReady || g_currentMediaPath.empty()) return;

    const auto saved = g_resumePositions.find(g_currentMediaPath);
    if (saved == g_resumePositions.end()) return;

    const double duration = std::max(0.0, MpvGetDouble("duration", 0.0));
    if (!ResumePositionShouldBeKept(saved->second, duration)) {
        g_resumePositions.erase(saved);
        g_resumePositionsDirty = true;
        FlushResumePositions();
        return;
    }

    double position = saved->second;
    if (duration > 0.0) position = std::clamp(position, 0.0, duration);

    if (MpvSetDouble("time-pos", position)) {
        g_timePos = position;
    }
}


bool MpvSetString(const char* name, const std::wstring& value) {
    if (!g_mpvReady || !g_mpvApi.set_property_string) return false;
    const std::string utf8 = WideToUtf8(value);
    return g_mpvApi.set_property_string(g_mpv, name, utf8.c_str()) >= 0;
}

bool MpvCommand(const std::vector<std::string>& args) {
    if (!g_mpvReady || args.empty()) return false;
    std::vector<const char*> p;
    p.reserve(args.size() + 1);
    for (const auto& s : args) p.push_back(s.c_str());
    p.push_back(nullptr);
    return g_mpvApi.command(g_mpv, p.data()) >= 0;
}


std::string MpvEscapeAssFallback(std::string text) {
    // Only used if libmpv's escape-ass command is unavailable. Preserve normal
    // filename text while neutralising the ASS control characters that could
    // otherwise turn a media title into formatting instructions.
    for (char& ch : text) {
        if (ch == '{') ch = '(';
        else if (ch == '}') ch = ')';
        else if (ch == '\\') ch = '/';
        else if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
    }
    return text;
}

std::string MpvEscapeAss(const std::string& text) {
    if (!g_mpvReady || !g_mpvApi.command_node || !g_mpvApi.free_node_contents) {
        return MpvEscapeAssFallback(text);
    }

    mpv_node values[2]{};
    values[0].format = MPV_FORMAT_STRING;
    values[0].u.string = const_cast<char*>("escape-ass");
    values[1].format = MPV_FORMAT_STRING;
    values[1].u.string = const_cast<char*>(text.c_str());

    mpv_node_list list{};
    list.num = 2;
    list.values = values;

    mpv_node args{};
    args.format = MPV_FORMAT_NODE_ARRAY;
    args.u.list = &list;

    mpv_node result{};
    if (g_mpvApi.command_node(g_mpv, &args, &result) < 0) {
        return MpvEscapeAssFallback(text);
    }

    std::string escaped = MpvEscapeAssFallback(text);
    if (result.format == MPV_FORMAT_STRING && result.u.string) {
        escaped = result.u.string;
    }
    g_mpvApi.free_node_contents(&result);
    return escaped;
}

int MpvSetTextOverlay(const std::string& format, const std::string& data) {
    if (!g_mpvReady || !g_mpvApi.command) return -12; // MPV_ERROR_COMMAND

    // The documented osd-overlay interface prefers named arguments, but the
    // bundled libmpv rejects our MPV_FORMAT_NODE_MAP call with
    // MPV_ERROR_INVALID_PARAMETER. Use the ordinary array API here instead.
    // Array arguments are already split and passed directly to mpv's argument
    // parsers, so DATA can contain spaces, newlines, and ASS backslashes without
    // input.conf quoting/escaping. Keep every positional field explicit so this
    // matches the command signature used by the bundled libmpv.
    const std::string id = std::to_string(kPlaylistOverlayId);
    const char* args[] = {
        "osd-overlay",
        id.c_str(),
        format.c_str(),
        data.c_str(),
        "0",       // res_x: derive from res_y / display aspect
        "720",     // res_y: documented default PlayResY
        "0",       // z
        "no",      // hidden
        "no",      // compute_bounds
        nullptr
    };
    return g_mpvApi.command(g_mpv, args);
}

std::wstring MpvErrorText(int code) {
    if (code >= 0) return {};
    if (g_mpvApi.error_string) {
        const char* text = g_mpvApi.error_string(code);
        if (text && *text) return Utf8ToWide(text);
    }
    return L"mpv error " + std::to_wstring(code);
}

void RemovePlaylistOverlay() {
    if (!g_mpvReady) return;
    MpvSetTextOverlay("none", "");
}

void UpdateOsdPosition() {
    if (!g_mpvReady || !g_video) return;

    RECT r{};
    GetClientRect(g_video, &r);
    const int height = std::max(0, static_cast<int>(r.bottom - r.top));
    if (height <= 0) return;

    auto SetOsdOption = [](const char* name, const std::string& value) {
        MpvCommand({"set", name, value});
    };

    const UINT dpi = GetDpiForWindow(g_video);
    const int scaleDpi = static_cast<int>(dpi ? dpi : 96);
    const int fontPx = std::max(24, MulDiv(g_osdFontSize, scaleDpi, 96));

    // libass leaves visible space above a top-aligned glyph. The previous
    // compensation was far too small; reference testing showed roughly
    // 21 px of extra apparent top spacing with a 72 px OSD at 96 DPI.
    const int glyphTopPadding =
        std::max(4, static_cast<int>(static_cast<double>(fontPx) * 0.29 + 0.5));

    if (g_mediaInfoVisible) {
        // The stats overlay is a large multi-line block, so don't inherit the
        // normal Golden Centre position. Give Media Info its own compact,
        // DPI-scaled top-left safe area so the full block has room below it.
        const int statsInset = std::max(12, MulDiv(18, scaleDpi, 96));

        SetOsdOption("osd-align-x", "left");
        SetOsdOption("osd-align-y", "top");
        SetOsdOption("osd-margin-x", std::to_string(statsInset));
        SetOsdOption("osd-margin-y", std::to_string(statsInset));
    } else if (g_osdPosition == OsdPosition::TopLeft) {
        // Aim for the visible text to be equally inset from the top and left.
        const int visibleInset = std::max(18, MulDiv(28, scaleDpi, 96));
        const int rawTopMargin = std::max(2, visibleInset - glyphTopPadding);

        SetOsdOption("osd-align-x", "left");
        SetOsdOption("osd-align-y", "top");
        SetOsdOption("osd-margin-x", std::to_string(visibleInset));
        SetOsdOption("osd-margin-y", std::to_string(rawTopMargin));
    } else {
        SetOsdOption("osd-align-x", "center");
        SetOsdOption("osd-align-y", "top");
        SetOsdOption("osd-margin-x", "0");

        // Place the *visible* OSD at the golden-ratio conjugate:
        // 38.2% down from the top, visually around one-third of the screen.
        const int visibleGoldenY =
            std::max(24, static_cast<int>(static_cast<double>(height) * 0.382 + 0.5));
        const int rawGoldenMargin =
            std::max(2, visibleGoldenY - glyphTopPadding);

        SetOsdOption("osd-margin-y", std::to_string(rawGoldenMargin));
    }
}

void ShowOsdText(const std::wstring& text, int durationMs = 750) {
    if (!g_mpvReady || text.empty()) return;

    // Custom playlist overlays sit above mpv's builtin OSD. Temporarily remove
    // a persistent playlist so volume/seek/speed messages remain readable,
    // then let the restore timer rebuild it after the transient message ends.
    if (g_playlistOsdVisible) RemovePlaylistOverlay();
    UpdateOsdPosition();

    MpvCommand({"show-text", WideToUtf8(text), std::to_string(durationMs)});

    if (g_playlistOsdVisible && g_main) {
        KillTimer(g_main, TIMER_PLAYLIST_OSD_RESTORE);
        SetTimer(g_main, TIMER_PLAYLIST_OSD_RESTORE,
                 static_cast<UINT>(std::max(50, durationMs + 60)), nullptr);
    }
}

const mpv_node* MapValue(const mpv_node& node, const char* key) {
    if (node.format != MPV_FORMAT_NODE_MAP || !node.u.list || !node.u.list->keys) return nullptr;
    for (int i = 0; i < node.u.list->num; ++i) {
        if (node.u.list->keys[i] && strcmp(node.u.list->keys[i], key) == 0) {
            return &node.u.list->values[i];
        }
    }
    return nullptr;
}

std::string NodeString(const mpv_node* n) {
    return (n && n->format == MPV_FORMAT_STRING && n->u.string) ? std::string(n->u.string) : std::string();
}

int64_t NodeInt(const mpv_node* n, int64_t fallback = 0) {
    return (n && n->format == MPV_FORMAT_INT64) ? n->u.int64 : fallback;
}

double NodeDouble(const mpv_node* n, double fallback = 0.0) {
    if (!n) return fallback;
    if (n->format == MPV_FORMAT_DOUBLE) return n->u.double_;
    if (n->format == MPV_FORMAT_INT64) return static_cast<double>(n->u.int64);
    return fallback;
}

bool NodeFlag(const mpv_node* n, bool fallback = false) {
    return (n && n->format == MPV_FORMAT_FLAG) ? n->u.flag != 0 : fallback;
}


void SetMediaWindowTitle(const std::wstring& path);
std::wstring AbsoluteLocalPath(const std::wstring& path);

std::wstring CutterTimestamp(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const long long totalMs = static_cast<long long>(std::llround(seconds * 1000.0));
    const long long hours = totalMs / 3600000;
    const long long minutes = (totalMs / 60000) % 60;
    const long long secs = (totalMs / 1000) % 60;
    const long long ms = totalMs % 1000;
    wchar_t buf[64]{};
    swprintf_s(buf, L"%02lld:%02lld:%02lld.%03lld", hours, minutes, secs, ms);
    return buf;
}

const wchar_t* CutterModeText(CutterOutputMode mode) {
    switch (mode) {
    case CutterOutputMode::Source: return L"Source";
    case CutterOutputMode::Mkv: return L"MKV";
    case CutterOutputMode::Mp4Copy: return L"MP4";
    case CutterOutputMode::Mp4H264: return L"MP4 H.264";
    case CutterOutputMode::WebM: return L"WebM";
    case CutterOutputMode::ExtractAudio: return L"Extract audio";
    case CutterOutputMode::M4A: return L"Audio to M4A";
    }
    return L"MP4 H.264";
}

const wchar_t* CutterAudioModeText(CutterAudioMode mode) {
    switch (mode) {
    case CutterAudioMode::Auto: return L"Auto";
    case CutterAudioMode::Copy: return L"Copy";
    case CutterAudioMode::None: return L"None";
    }
    return L"Auto";
}

int CutterEffectiveAudioKbps(CutterOutputMode mode, int overrideKbps) {
    if (overrideKbps > 0) return overrideKbps;
    (void)mode;
    // YouTube-style delivery default. Current YouTube playback formats are
    // typically around 128 kbps for both AAC and Opus, so use one predictable
    // automatic bitrate unless the user explicitly overrides it.
    return 128;
}

bool CutterReencodesVideo() {
    return g_cutMode == CutterOutputMode::Mp4H264 ||
           g_cutMode == CutterOutputMode::WebM;
}

bool CutterSupportsTargetSize() {
    return CutterReencodesVideo();
}

std::wstring CutterOutputExtension() {
    if (g_cutMode == CutterOutputMode::Mkv) return L"mkv";
    if (g_cutMode == CutterOutputMode::Mp4Copy ||
        g_cutMode == CutterOutputMode::Mp4H264) return L"mp4";
    if (g_cutMode == CutterOutputMode::WebM) return L"webm";
    if (g_cutMode == CutterOutputMode::M4A) return L"m4a";
    if (g_cutMode == CutterOutputMode::ExtractAudio) return L"mka";

    const std::wstring source = !g_cutInputs.empty()
        ? g_cutInputs.front() : g_currentMediaPath;
    std::wstring ext = std::filesystem::path(source).extension().wstring();
    if (!ext.empty() && ext.front() == L'.') ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    if (ext.empty()) ext = L"mkv";
    return ext;
}

void SaveSettings();
void PollMpv();
void ResetKeyframeIndexCache();
void SyncCutterSubtitleContextControls();
void SyncCutterContextVisibility();
void EnsureCutterManagedAudioSource();
void ResetCutterManagedAudioTracksForSource();
void EnsureCutterAudioTracksInitialized();
CutterManagedAudioTrack* ActiveManagedAudioSyncTrack();
const CutterManagedAudioTrack* ActiveManagedAudioSyncTrackConst();
void LoadManagedAudioSyncToControls();
void CutterApplyAudioPreviewDelay();
void SyncCutterAudioContextControls();
bool CutterCommitAudioSyncOffsetEdit(bool showError);
void ResetCutterManagedSubtitleTracksForSource(bool preserveExternal = false);
void EnsureCutterManagedSubtitleSource();
void EnsureCutterSubtitleTracksInitialized();
bool CutterManagedSubtitleDriftReady(
    const CutterManagedSubtitleTrack& track);
void StoreLegacySubtitleSyncToManaged();
void LoadManagedSubtitleSyncToLegacy();
bool CutterHasActiveSubtitleSyncTarget();
void CutterSelectSubtitleForPreview(const std::wstring& rawPath);
void CutterSelectActiveSubtitleForPreview();
std::wstring CutterTempPath(const wchar_t* suffix);

bool CutterIsSubtitlePath(const std::wstring& path) {
    std::wstring ext = std::filesystem::path(path).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return ext == L".srt" || ext == L".ass" || ext == L".ssa" ||
           ext == L".vtt" || ext == L".sub";
}

std::wstring CutterFindMatchingSubtitle(const std::wstring& videoPath) {
    if (videoPath.empty()) return L"";
    const std::filesystem::path video(videoPath);
    const std::filesystem::path base = video.parent_path() / video.stem();
    const wchar_t* exts[] = { L".srt", L".ass", L".ssa", L".vtt", L".sub" };
    for (const wchar_t* ext : exts) {
        const std::wstring candidate = base.wstring() + ext;
        if (FileExists(candidate)) return candidate;
    }
    return L"";
}

void CutterAutoPairSubtitleForInput(const std::wstring& videoPath) {
    if (videoPath.empty() || CutterIsSubtitlePath(videoPath)) return;
    const std::wstring paired = CutterFindMatchingSubtitle(videoPath);
    const bool changed = paired != g_cutSubtitlePath ||
                         g_cutSubtitlesEnabled != !paired.empty();
    if (!paired.empty()) {
        g_cutSubtitlePath = paired;
        g_cutSubtitlesEnabled = true;
    } else {
        // Do not carry a subtitle file from the previous video into a newly
        // opened source. Preserve the user's mode/title preference, only clear
        // the source-specific path and activation state.
        g_cutSubtitlePath.clear();
        g_cutSubtitlesEnabled = false;
    }
    if (changed) {
        g_cutSubtitleSyncASet = false;
        g_cutSubtitleSyncBSet = false;
        g_cutSubtitleDriftEnabled = false;
        SaveSettings();
    }
    SyncCutterSubtitleContextControls();
    if (g_cutSubtitles) InvalidateRect(g_cutSubtitles, nullptr, FALSE);
}

std::wstring CutterSubtitleFilterPath(const std::wstring& rawPath) {
    std::wstring path = AbsoluteLocalPath(rawPath);
    std::replace(path.begin(), path.end(), L'\\', L'/');
    std::wstring escaped;
    escaped.reserve(path.size() + 8);
    for (wchar_t ch : path) {
        if (ch == L':') escaped += L"\\:";
        else if (ch == L'\'') escaped += L"\\'";
        else escaped.push_back(ch);
    }
    return escaped;
}

struct CutterSubtitleSyncSpec {
    int constantOffsetMs = 0;
    bool driftEnabled = false;
    double videoASeconds = 0.0;
    double videoBSeconds = 0.0;
    int offsetAMs = 0;
    int offsetBMs = 0;
    int timelineShiftMs = 0;
};

bool CutterSubtitleDriftGeometryValid(
    double videoASeconds,
    int offsetAMs,
    double videoBSeconds,
    int offsetBMs) {

    double v1 = videoASeconds;
    double v2 = videoBSeconds;
    double o1 = static_cast<double>(offsetAMs) / 1000.0;
    double o2 = static_cast<double>(offsetBMs) / 1000.0;

    if (v2 < v1) {
        std::swap(v1, v2);
        std::swap(o1, o2);
    }

    const double videoSpan = v2 - v1;
    const double source1 = v1 - o1;
    const double source2 = v2 - o2;
    const double sourceSpan = source2 - source1;

    return videoSpan > 0.050 &&
           sourceSpan > 0.050;
}

bool CutterSubtitleDriftReady() {
    if (!g_cutSubtitleSyncASet || !g_cutSubtitleSyncBSet) {
        return false;
    }

    return CutterSubtitleDriftGeometryValid(
        g_cutSubtitleSyncAVideoSeconds,
        g_cutSubtitleSyncAOffsetMs,
        g_cutSubtitleSyncBVideoSeconds,
        g_cutSubtitleSyncBOffsetMs);
}

int CutterSubtitlePreviewDelayMsAt(double videoSeconds) {
    if (!g_cutSubtitleDriftEnabled || !CutterSubtitleDriftReady()) {
        return g_cutSubtitleOffsetMs;
    }

    double v1 = g_cutSubtitleSyncAVideoSeconds;
    double v2 = g_cutSubtitleSyncBVideoSeconds;
    double o1 = static_cast<double>(g_cutSubtitleSyncAOffsetMs);
    double o2 = static_cast<double>(g_cutSubtitleSyncBOffsetMs);
    if (v2 < v1) {
        std::swap(v1, v2);
        std::swap(o1, o2);
    }

    if (videoSeconds <= v1) return static_cast<int>(std::llround(o1));
    if (videoSeconds >= v2) return static_cast<int>(std::llround(o2));

    const double source1 = v1 - o1 / 1000.0;
    const double source2 = v2 - o2 / 1000.0;
    const double sourceSpan = source2 - source1;
    const double correctedSpan = v2 - v1;
    if (std::abs(sourceSpan) < 0.000001 ||
        std::abs(correctedSpan) < 0.000001) {
        return static_cast<int>(std::llround(o1));
    }

    const double ratio = correctedSpan / sourceSpan;
    const double sourceAtVideo =
        source1 + (videoSeconds - v1) / ratio;
    return static_cast<int>(
        std::llround((videoSeconds - sourceAtVideo) * 1000.0));
}

void CutterApplySubtitlePreviewDelay() {
    if (!g_mpv || !CutterHasActiveSubtitleSyncTarget()) return;
    const int delayMs =
        CutterSubtitlePreviewDelayMsAt(
            std::max(0.0, MpvGetDouble("time-pos", g_timePos)));
    MpvSetDouble("sub-delay", static_cast<double>(delayMs) / 1000.0);
    g_subtitleDelayTouched = delayMs != 0;
}

bool CutterParseSubtitleOffsetText(int& valueMs, bool showError) {
    valueMs = g_cutSubtitleOffsetMs;
    if (!g_cutSubOffsetEdit) return true;

    wchar_t text[64]{};
    GetWindowTextW(
        g_cutSubOffsetEdit, text,
        static_cast<int>(std::size(text)));

    std::wstring value(text);
    value.erase(
        std::remove_if(
            value.begin(), value.end(),
            [](wchar_t ch) { return iswspace(ch) != 0; }),
        value.end());

    if (value.empty()) value = L"0";

    wchar_t* end = nullptr;
    const long parsed = wcstol(value.c_str(), &end, 10);
    if (!end ||
        end == value.c_str() ||
        *end != L'\0' ||
        parsed < -600000L ||
        parsed > 600000L) {
        if (showError) {
            MessageBoxW(
                g_main,
                L"Subtitle offset must be a whole number from -600000 to 600000 ms.",
                kAppTitle,
                MB_OK | MB_ICONWARNING);
        }
        return false;
    }

    valueMs = static_cast<int>(parsed);
    return true;
}

void CutterSetSubtitleOffsetMs(int valueMs, bool disableDrift = true) {
    g_cutSubtitleOffsetMs =
        std::clamp(valueMs, -600000, 600000);

    if (disableDrift) {
        g_cutSubtitleDriftEnabled = false;
    }

    if (g_cutSubOffsetEdit) {
        SetWindowTextW(
            g_cutSubOffsetEdit,
            std::to_wstring(g_cutSubtitleOffsetMs).c_str());
    }

    StoreLegacySubtitleSyncToManaged();
    CutterApplySubtitlePreviewDelay();
    SyncCutterSubtitleContextControls();
    SaveSettings();
}

void CutterAdjustSubtitleOffsetMs(int deltaMs) {
    int current = g_cutSubtitleOffsetMs;
    CutterParseSubtitleOffsetText(current, false);
    CutterSetSubtitleOffsetMs(current + deltaMs, true);
}

std::wstring CutterSyncPointText(
    bool set,
    double videoSeconds,
    int offsetMs) {

    if (!set) return L"--";

    const int totalMs = static_cast<int>(
        std::llround(std::max(0.0, videoSeconds) * 1000.0));
    const int minutes = totalMs / 60000;
    const int seconds = (totalMs / 1000) % 60;
    const int millis = totalMs % 1000;

    wchar_t buffer[96]{};
    swprintf_s(
        buffer,
        L"%d:%02d.%03d %+.0fms",
        minutes,
        seconds,
        millis,
        static_cast<double>(offsetMs));
    return buffer;
}

void CutterCaptureSubtitleSyncPoint(bool first) {
    if (!CutterHasActiveSubtitleSyncTarget()) {
        MessageBoxW(
            g_main,
            L"Select a sync-capable subtitle in SUBS SYNC first.",
            kAppTitle,
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    int offset = g_cutSubtitleOffsetMs;
    if (!CutterParseSubtitleOffsetText(offset, true)) return;
    g_cutSubtitleOffsetMs = offset;

    const double videoTime =
        std::max(0.0, MpvGetDouble("time-pos", g_timePos));

    if (first) {
        g_cutSubtitleSyncASet = true;
        g_cutSubtitleSyncAVideoSeconds = videoTime;
        g_cutSubtitleSyncAOffsetMs = offset;
    } else {
        g_cutSubtitleSyncBSet = true;
        g_cutSubtitleSyncBVideoSeconds = videoTime;
        g_cutSubtitleSyncBOffsetMs = offset;
    }

    if (g_cutSubtitleSyncASet &&
        g_cutSubtitleSyncBSet &&
        CutterSubtitleDriftReady()) {
        g_cutSubtitleDriftEnabled = true;
    }

    StoreLegacySubtitleSyncToManaged();
    SyncCutterSubtitleContextControls();
    CutterApplySubtitlePreviewDelay();

    ShowOsdText(
        std::wstring(first ? L"Subtitle sync A: " : L"Subtitle sync B: ") +
        CutterSyncPointText(true, videoTime, offset),
        1800);
}

void CutterClearSubtitleDrift() {
    g_cutSubtitleSyncASet = false;
    g_cutSubtitleSyncBSet = false;
    g_cutSubtitleDriftEnabled = false;
    StoreLegacySubtitleSyncToManaged();
    SyncCutterSubtitleContextControls();
    CutterApplySubtitlePreviewDelay();
    ShowOsdText(L"Subtitle drift points cleared", 1300);
}

int64_t CutterTransformSubtitleMs(
    int64_t oldMs,
    const CutterSubtitleSyncSpec& spec) {

    double corrected = static_cast<double>(oldMs);

    if (spec.driftEnabled) {
        double v1 = spec.videoASeconds * 1000.0;
        double v2 = spec.videoBSeconds * 1000.0;
        double o1 = static_cast<double>(spec.offsetAMs);
        double o2 = static_cast<double>(spec.offsetBMs);

        if (v2 < v1) {
            std::swap(v1, v2);
            std::swap(o1, o2);
        }

        const double s1 = v1 - o1;
        const double s2 = v2 - o2;
        const double correctedSpan = v2 - v1;
        const double sourceSpan = s2 - s1;

        if (correctedSpan <= 1.0 ||
            sourceSpan <= 1.0) {
            corrected +=
                static_cast<double>(spec.constantOffsetMs);
        } else if (oldMs <= s1) {
            corrected += o1;
        } else if (oldMs >= s2) {
            corrected += o2;
        } else {
            const double ratio =
                correctedSpan / sourceSpan;
            corrected =
                v1 +
                (static_cast<double>(oldMs) - s1) *
                    ratio;
        }
    } else {
        corrected +=
            static_cast<double>(spec.constantOffsetMs);
    }

    corrected -=
        static_cast<double>(spec.timelineShiftMs);

    return std::max<int64_t>(
        0,
        static_cast<int64_t>(
            std::llround(corrected)));
}

bool CutterReadBinaryFile(
    const std::wstring& path,
    std::string& data) {

    std::ifstream file(
        std::filesystem::path(path),
        std::ios::binary);
    if (!file) return false;
    data.assign(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
    return static_cast<bool>(file) || file.eof();
}

bool CutterWriteBinaryFile(
    const std::wstring& path,
    const std::string& data) {

    std::ofstream file(
        std::filesystem::path(path),
        std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(
        data.data(),
        static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(file);
}

std::string CutterTrimAscii(const std::string& value) {
    size_t first = 0;
    while (first < value.size() &&
           (value[first] == ' ' || value[first] == '\t')) {
        ++first;
    }

    size_t last = value.size();
    while (last > first &&
           (value[last - 1] == ' ' ||
            value[last - 1] == '\t' ||
            value[last - 1] == '\r')) {
        --last;
    }
    return value.substr(first, last - first);
}

bool CutterParseIntegerAscii(
    const std::string& text,
    long long& value) {

    if (text.empty()) return false;
    char* end = nullptr;
    const long long parsed =
        std::strtoll(text.c_str(), &end, 10);
    if (!end || end == text.c_str() || *end != '\0') {
        return false;
    }
    value = parsed;
    return true;
}

bool CutterParseSubtitleClock(
    const std::string& raw,
    bool allowNoHours,
    int64_t& ms) {

    const std::string text = CutterTrimAscii(raw);
    if (text.empty()) return false;

    const size_t dot = text.find_last_of(".,");
    if (dot == std::string::npos) return false;

    const std::string whole = text.substr(0, dot);
    std::string fraction = text.substr(dot + 1);
    if (fraction.empty() || fraction.size() > 3) return false;
    for (char ch : fraction) {
        if (ch < '0' || ch > '9') return false;
    }
    while (fraction.size() < 3) fraction.push_back('0');

    std::vector<long long> parts;
    size_t begin = 0;
    while (begin <= whole.size()) {
        const size_t colon = whole.find(':', begin);
        const std::string part = whole.substr(
            begin,
            colon == std::string::npos
                ? std::string::npos
                : colon - begin);
        long long parsed = 0;
        if (!CutterParseIntegerAscii(part, parsed)) {
            return false;
        }
        parts.push_back(parsed);
        if (colon == std::string::npos) break;
        begin = colon + 1;
    }

    if (parts.size() != 3 &&
        !(allowNoHours && parts.size() == 2)) {
        return false;
    }

    long long hours = 0;
    long long minutes = 0;
    long long seconds = 0;
    if (parts.size() == 3) {
        hours = parts[0];
        minutes = parts[1];
        seconds = parts[2];
    } else {
        minutes = parts[0];
        seconds = parts[1];
    }

    if (hours < 0 ||
        minutes < 0 || minutes > 59 ||
        seconds < 0 || seconds > 59) {
        return false;
    }

    long long fracMs = 0;
    if (!CutterParseIntegerAscii(fraction, fracMs)) {
        return false;
    }

    ms =
        ((hours * 60 + minutes) * 60 + seconds) * 1000 +
        fracMs;
    return true;
}

std::string CutterFormatSubtitleClock(
    int64_t ms,
    char fractionSeparator) {

    ms = std::max<int64_t>(0, ms);
    const long long hours = ms / 3600000;
    const long long minutes = (ms / 60000) % 60;
    const long long seconds = (ms / 1000) % 60;
    const long long millis = ms % 1000;

    char buffer[64]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%02lld:%02lld:%02lld%c%03lld",
        hours,
        minutes,
        seconds,
        fractionSeparator,
        millis);
    return buffer;
}

bool CutterParseAssClock(
    const std::string& raw,
    int64_t& ms) {

    const std::string text = CutterTrimAscii(raw);
    const size_t firstColon = text.find(':');
    const size_t secondColon =
        firstColon == std::string::npos
            ? std::string::npos
            : text.find(':', firstColon + 1);
    const size_t dot =
        secondColon == std::string::npos
            ? std::string::npos
            : text.find('.', secondColon + 1);

    if (firstColon == std::string::npos ||
        secondColon == std::string::npos ||
        dot == std::string::npos) {
        return false;
    }

    long long h = 0;
    long long m = 0;
    long long sec = 0;
    if (!CutterParseIntegerAscii(
            text.substr(0, firstColon), h) ||
        !CutterParseIntegerAscii(
            text.substr(
                firstColon + 1,
                secondColon - firstColon - 1), m) ||
        !CutterParseIntegerAscii(
            text.substr(
                secondColon + 1,
                dot - secondColon - 1), sec)) {
        return false;
    }

    std::string fraction = text.substr(dot + 1);
    if (fraction.empty() || fraction.size() > 3) return false;
    for (char ch : fraction) {
        if (ch < '0' || ch > '9') return false;
    }
    while (fraction.size() < 3) fraction.push_back('0');

    long long fracMs = 0;
    if (!CutterParseIntegerAscii(fraction, fracMs)) {
        return false;
    }

    if (h < 0 || m < 0 || m > 59 ||
        sec < 0 || sec > 59) {
        return false;
    }

    ms = ((h * 60 + m) * 60 + sec) * 1000 + fracMs;
    return true;
}

std::string CutterFormatAssClock(int64_t ms) {
    ms = std::max<int64_t>(0, ms);
    // ASS/SSA stores centiseconds. Round to the closest representable value.
    ms = ((ms + 5) / 10) * 10;

    const long long hours = ms / 3600000;
    const long long minutes = (ms / 60000) % 60;
    const long long seconds = (ms / 1000) % 60;
    const long long centiseconds = (ms % 1000) / 10;

    char buffer[64]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%lld:%02lld:%02lld.%02lld",
        hours,
        minutes,
        seconds,
        centiseconds);
    return buffer;
}

bool CutterRewriteArrowSubtitleLine(
    std::string& line,
    bool vtt,
    const CutterSubtitleSyncSpec& spec) {

    const size_t arrow = line.find("-->");
    if (arrow == std::string::npos) return true;

    size_t startBegin = 0;
    while (startBegin < arrow &&
           (line[startBegin] == ' ' ||
            line[startBegin] == '\t')) {
        ++startBegin;
    }

    size_t startEnd = arrow;
    while (startEnd > startBegin &&
           (line[startEnd - 1] == ' ' ||
            line[startEnd - 1] == '\t')) {
        --startEnd;
    }

    size_t endBegin = arrow + 3;
    while (endBegin < line.size() &&
           (line[endBegin] == ' ' ||
            line[endBegin] == '\t')) {
        ++endBegin;
    }

    size_t endEnd = endBegin;
    while (endEnd < line.size() &&
           line[endEnd] != ' ' &&
           line[endEnd] != '\t' &&
           line[endEnd] != '\r') {
        ++endEnd;
    }

    int64_t startMs = 0;
    int64_t endMs = 0;
    const bool startOk =
        CutterParseSubtitleClock(
            line.substr(startBegin, startEnd - startBegin),
            vtt,
            startMs);
    const bool endOk =
        CutterParseSubtitleClock(
            line.substr(endBegin, endEnd - endBegin),
            vtt,
            endMs);
    if (!startOk && !endOk) {
        // Dialogue/comment text is allowed to contain "-->". Only treat a line
        // as a cue timing line when at least one side actually looks like time.
        return true;
    }
    if (!startOk || !endOk) {
        return false;
    }

    const char separator = vtt ? '.' : ',';
    const std::string newStart =
        CutterFormatSubtitleClock(
            CutterTransformSubtitleMs(startMs, spec),
            separator);
    const std::string newEnd =
        CutterFormatSubtitleClock(
            CutterTransformSubtitleMs(endMs, spec),
            separator);

    line.replace(
        endBegin,
        endEnd - endBegin,
        newEnd);
    line.replace(
        startBegin,
        startEnd - startBegin,
        newStart);
    return true;
}

bool CutterStartsWithAsciiNoCase(
    const std::string& text,
    const char* prefix) {

    const size_t n = std::strlen(prefix);
    if (text.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = text[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a =
            static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b =
            static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool CutterRewriteAssDialogueLine(
    std::string& line,
    const CutterSubtitleSyncSpec& spec) {

    if (!CutterStartsWithAsciiNoCase(
            line, "Dialogue:")) {
        return true;
    }

    const size_t prefixEnd = line.find(':');
    const size_t comma1 =
        prefixEnd == std::string::npos
            ? std::string::npos
            : line.find(',', prefixEnd + 1);
    const size_t comma2 =
        comma1 == std::string::npos
            ? std::string::npos
            : line.find(',', comma1 + 1);
    const size_t comma3 =
        comma2 == std::string::npos
            ? std::string::npos
            : line.find(',', comma2 + 1);

    if (comma1 == std::string::npos ||
        comma2 == std::string::npos ||
        comma3 == std::string::npos) {
        return false;
    }

    int64_t startMs = 0;
    int64_t endMs = 0;
    if (!CutterParseAssClock(
            line.substr(
                comma1 + 1,
                comma2 - comma1 - 1),
            startMs) ||
        !CutterParseAssClock(
            line.substr(
                comma2 + 1,
                comma3 - comma2 - 1),
            endMs)) {
        return false;
    }

    const std::string newStart =
        CutterFormatAssClock(
            CutterTransformSubtitleMs(startMs, spec));
    const std::string newEnd =
        CutterFormatAssClock(
            CutterTransformSubtitleMs(endMs, spec));

    line.replace(
        comma2 + 1,
        comma3 - comma2 - 1,
        newEnd);
    line.replace(
        comma1 + 1,
        comma2 - comma1 - 1,
        newStart);
    return true;
}

bool CutterParseMicroDvdLine(
    const std::string& line,
    long long& startFrame,
    long long& endFrame,
    size_t& secondBraceEnd) {

    if (line.empty() || line[0] != '{') return false;
    const size_t close1 = line.find('}', 1);
    if (close1 == std::string::npos ||
        close1 + 1 >= line.size() ||
        line[close1 + 1] != '{') {
        return false;
    }

    const size_t close2 =
        line.find('}', close1 + 2);
    if (close2 == std::string::npos) return false;

    if (!CutterParseIntegerAscii(
            line.substr(1, close1 - 1),
            startFrame) ||
        !CutterParseIntegerAscii(
            line.substr(
                close1 + 2,
                close2 - close1 - 2),
            endFrame)) {
        return false;
    }

    secondBraceEnd = close2 + 1;
    return true;
}

double CutterMicroDvdDeclaredFps(
    const std::string& data) {

    size_t pos = 0;
    int inspected = 0;
    while (pos < data.size() && inspected < 12) {
        const size_t end = data.find('\n', pos);
        std::string line =
            data.substr(
                pos,
                end == std::string::npos
                    ? std::string::npos
                    : end - pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const std::string trimmed =
            CutterTrimAscii(line);
        if (!trimmed.empty()) {
            ++inspected;
            long long a = 0;
            long long b = 0;
            size_t payload = 0;
            if (CutterParseMicroDvdLine(
                    trimmed, a, b, payload) &&
                a == b &&
                (a == 0 || a == 1)) {
                const std::string value =
                    CutterTrimAscii(
                        trimmed.substr(payload));
                char* parseEnd = nullptr;
                const double fps =
                    std::strtod(
                        value.c_str(),
                        &parseEnd);
                if (parseEnd &&
                    parseEnd != value.c_str() &&
                    *parseEnd == '\0' &&
                    fps >= 1.0 &&
                    fps <= 240.0) {
                    return fps;
                }
            }
        }

        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return 0.0;
}

double CutterCurrentVideoFps() {
    double fps =
        MpvGetDouble("container-fps", 0.0);
    if (fps <= 0.0) {
        fps =
            MpvGetDouble(
                "estimated-vf-fps", 0.0);
    }
    return fps;
}

bool CutterRewriteMicroDvd(
    std::string& data,
    const CutterSubtitleSyncSpec& spec,
    double videoFps,
    std::wstring& error) {

    const double declaredFps =
        CutterMicroDvdDeclaredFps(data);
    const double fps =
        declaredFps > 0.0
            ? declaredFps
            : videoFps;

    if (fps <= 0.0) {
        error =
            L"Cannot apply subtitle timing correction to this MicroDVD file because the frame rate could not be determined.";
        return false;
    }

    std::string output;
    output.reserve(data.size() + 256);

    size_t pos = 0;
    bool sawCue = false;
    while (pos < data.size()) {
        const size_t end = data.find('\n', pos);
        std::string line =
            data.substr(
                pos,
                end == std::string::npos
                    ? std::string::npos
                    : end - pos);
        const bool hadCr =
            !line.empty() && line.back() == '\r';
        if (hadCr) line.pop_back();

        long long startFrame = 0;
        long long endFrame = 0;
        size_t payload = 0;
        if (CutterParseMicroDvdLine(
                line,
                startFrame,
                endFrame,
                payload)) {

            const std::string body =
                CutterTrimAscii(
                    line.substr(payload));
            char* parseEnd = nullptr;
            const double maybeFps =
                std::strtod(
                    body.c_str(),
                    &parseEnd);
            const bool fpsDeclaration =
                startFrame == endFrame &&
                (startFrame == 0 ||
                 startFrame == 1) &&
                parseEnd &&
                parseEnd != body.c_str() &&
                *parseEnd == '\0' &&
                maybeFps >= 1.0 &&
                maybeFps <= 240.0;

            if (!fpsDeclaration) {
                sawCue = true;
                const int64_t startMs =
                    static_cast<int64_t>(
                        std::llround(
                            static_cast<double>(
                                startFrame) *
                            1000.0 / fps));
                const int64_t endMs =
                    static_cast<int64_t>(
                        std::llround(
                            static_cast<double>(
                                endFrame) *
                            1000.0 / fps));

                const long long newStart =
                    std::max<long long>(
                        0,
                        static_cast<long long>(
                            std::llround(
                                static_cast<double>(
                                    CutterTransformSubtitleMs(
                                        startMs, spec)) *
                                fps / 1000.0)));
                const long long newEnd =
                    std::max<long long>(
                        newStart,
                        static_cast<long long>(
                            std::llround(
                                static_cast<double>(
                                    CutterTransformSubtitleMs(
                                        endMs, spec)) *
                                fps / 1000.0)));

                line =
                    "{" +
                    std::to_string(newStart) +
                    "}{" +
                    std::to_string(newEnd) +
                    "}" +
                    line.substr(payload);
            }
        }

        output += line;
        if (hadCr) output += '\r';
        if (end != std::string::npos) {
            output += '\n';
            pos = end + 1;
        } else {
            break;
        }
    }

    if (!sawCue) {
        error =
            L"Subtitle sync currently supports text MicroDVD .sub files. The selected .sub file does not appear to be MicroDVD text.";
        return false;
    }

    data.swap(output);
    return true;
}

bool CutterRewriteSubtitleTiming(
    const std::wstring& sourcePath,
    const CutterSubtitleSyncSpec& spec,
    double videoFps,
    std::wstring& outputPath,
    std::wstring& error) {

    std::string data;
    if (!CutterReadBinaryFile(
            sourcePath, data)) {
        error =
            L"Could not read the subtitle file for timing correction.";
        return false;
    }

    if (data.size() >= 2 &&
        ((static_cast<unsigned char>(data[0]) == 0xFF &&
          static_cast<unsigned char>(data[1]) == 0xFE) ||
         (static_cast<unsigned char>(data[0]) == 0xFE &&
          static_cast<unsigned char>(data[1]) == 0xFF))) {
        error =
            L"Subtitle timing correction does not yet support UTF-16 subtitle files. Convert this subtitle to UTF-8, or use zero offset with drift correction off.";
        return false;
    }

    std::wstring ext =
        std::filesystem::path(
            sourcePath).extension().wstring();
    std::transform(
        ext.begin(), ext.end(), ext.begin(),
        [](wchar_t ch) {
            return static_cast<wchar_t>(
                towlower(ch));
        });

    if (ext == L".sub") {
        const std::filesystem::path src(sourcePath);
        const std::filesystem::path idx =
            src.parent_path() /
            (src.stem().wstring() + L".idx");
        if (FileExists(idx.wstring())) {
            error =
                L"The selected .sub file appears to be VobSub because a matching .idx file exists. Timing correction currently supports text MicroDVD .sub files only.";
            return false;
        }

        if (!CutterRewriteMicroDvd(
                data, spec, videoFps, error)) {
            return false;
        }
    } else {
        std::string output;
        output.reserve(data.size() + 256);
        size_t pos = 0;
        size_t rewritten = 0;

        while (pos < data.size()) {
            const size_t end =
                data.find('\n', pos);
            std::string line =
                data.substr(
                    pos,
                    end == std::string::npos
                        ? std::string::npos
                        : end - pos);
            const bool hadCr =
                !line.empty() &&
                line.back() == '\r';
            if (hadCr) line.pop_back();

            bool ok = true;
            const std::string before = line;
            if (ext == L".srt") {
                ok =
                    CutterRewriteArrowSubtitleLine(
                        line, false, spec);
            } else if (ext == L".vtt") {
                ok =
                    CutterRewriteArrowSubtitleLine(
                        line, true, spec);
            } else if (ext == L".ass" ||
                       ext == L".ssa") {
                ok =
                    CutterRewriteAssDialogueLine(
                        line, spec);
            } else {
                error =
                    L"Subtitle timing correction supports SRT, VTT, ASS, SSA, and text MicroDVD SUB files.";
                return false;
            }

            if (!ok) {
                error =
                    L"Could not parse one of the subtitle timing lines. The original subtitle file was not changed.";
                return false;
            }
            if (line != before) ++rewritten;

            output += line;
            if (hadCr) output += '\r';
            if (end != std::string::npos) {
                output += '\n';
                pos = end + 1;
            } else {
                break;
            }
        }

        if (rewritten == 0) {
            error =
                L"No subtitle cue timestamps were found to rewrite. The original subtitle file was not changed.";
            return false;
        }

        data.swap(output);
    }

    const std::wstring suffix =
        L".synced" + ext;
    outputPath =
        CutterTempPath(suffix.c_str());

    if (!CutterWriteBinaryFile(
            outputPath, data)) {
        error =
            L"Could not write the temporary synced subtitle file.";
        outputPath.clear();
        return false;
    }

    return true;
}

bool CutterValidateManagedAudioTracks(CutterOutputMode jobMode) {
    if (!g_cutAudioTracksInitialized) return true;
    if (g_cutInputs.size() > 1) {
        MessageBoxW(g_main,
                    L"Custom audio track selection is currently limited to single-file jobs.\n\n"
                    L"Use the default MULTI stream layout, or join/export first and manage tracks afterward.",
                    kAppTitle, MB_OK | MB_ICONINFORMATION);
        return false;
    }
    const bool anyIncluded = std::any_of(
        g_cutAudioTracks.begin(), g_cutAudioTracks.end(),
        [](const CutterManagedAudioTrack& track) { return track.included; });
    if (!anyIncluded &&
        (jobMode == CutterOutputMode::ExtractAudio ||
         jobMode == CutterOutputMode::M4A)) {
        MessageBoxW(g_main,
                    L"No audio tracks are included.\n\nChoose at least one track in TRACKS for an audio-only output.",
                    kAppTitle, MB_OK | MB_ICONINFORMATION);
        return false;
    }

    for (const auto& track : g_cutAudioTracks) {
        if (track.embedded || !track.included) continue;
        if (track.externalPath.empty() || !FileExists(track.externalPath)) {
            MessageBoxW(g_main,
                        (L"External audio file not found:\n\n" + track.externalPath).c_str(),
                        kAppTitle, MB_OK | MB_ICONWARNING);
            return false;
        }
    }
    return true;
}

bool CutterValidateSubtitles(CutterOutputMode jobMode) {
    if (g_cutSubtitleTracksInitialized) {
        const bool anyIncluded = std::any_of(
            g_cutSubtitleTracks.begin(),
            g_cutSubtitleTracks.end(),
            [](const CutterManagedSubtitleTrack& track) {
                return track.included;
            });
        if (!anyIncluded) return true;

        if (g_cutInputs.size() > 1) {
            MessageBoxW(
                g_main,
                L"Custom subtitle track selection is currently limited to single-file jobs.\n\n"
                L"Use the default MULTI stream layout, or join/export first and manage subtitles afterward.",
                kAppTitle,
                MB_OK | MB_ICONINFORMATION);
            return false;
        }

        if (jobMode == CutterOutputMode::ExtractAudio ||
            jobMode == CutterOutputMode::M4A) {
            MessageBoxW(
                g_main,
                L"Subtitles do not apply to audio-only output modes.",
                kAppTitle,
                MB_OK | MB_ICONINFORMATION);
            return false;
        }

        bool anySoft = false;
        int burnCount = 0;
        for (const auto& track : g_cutSubtitleTracks) {
            if (!track.included) continue;

            if (track.mode == CutterSubtitleMode::Burn) {
                ++burnCount;
                if (track.embedded) {
                    MessageBoxW(
                        g_main,
                        L"Burning embedded subtitle tracks is not enabled yet.\n\n"
                        L"Use Soft for the embedded track, or add an external subtitle for Burn.",
                        kAppTitle,
                        MB_OK | MB_ICONINFORMATION);
                    return false;
                }
            } else {
                anySoft = true;
            }

            if (!track.embedded) {
                if (track.externalPath.empty() ||
                    !FileExists(track.externalPath)) {
                    MessageBoxW(
                        g_main,
                        (L"External subtitle file not found:\n\n" +
                         track.externalPath).c_str(),
                        kAppTitle,
                        MB_OK | MB_ICONWARNING);
                    return false;
                }
                if (!CutterIsSubtitlePath(track.externalPath)) {
                    MessageBoxW(
                        g_main,
                        L"Subtitle files must be SRT, ASS, SSA, VTT, or SUB.",
                        kAppTitle,
                        MB_OK | MB_ICONWARNING);
                    return false;
                }
                std::error_code sizeError;
                const auto subtitleSize = std::filesystem::file_size(
                    std::filesystem::path(track.externalPath), sizeError);
                if (sizeError || subtitleSize == 0) {
                    MessageBoxW(
                        g_main,
                        (L"Subtitle file is empty or could not be read:\n\n" +
                         track.externalPath).c_str(),
                        kAppTitle,
                        MB_OK | MB_ICONWARNING);
                    return false;
                }
            }
        }

        if (burnCount > 1) {
            MessageBoxW(
                g_main,
                L"Only one subtitle track can be burned into the video.",
                kAppTitle,
                MB_OK | MB_ICONWARNING);
            return false;
        }

        if (anySoft) {
            const std::wstring ext = CutterOutputExtension();
            if (_wcsicmp(ext.c_str(), L"mkv") != 0 &&
                _wcsicmp(ext.c_str(), L"mp4") != 0) {
                MessageBoxW(
                    g_main,
                    L"Soft subtitle tracks require MKV or MP4 output.\n\n"
                    L"Choose MKV/MP4, or turn Soft subtitle rows Off.",
                    kAppTitle,
                    MB_OK | MB_ICONINFORMATION);
                return false;
            }
        }

        if (burnCount > 0 && !CutterReencodesVideo()) {
            MessageBoxW(
                g_main,
                L"Burn-in subtitles require video re-encoding.\n\n"
                L"Choose MP4 H.264 or WebM, or switch the Burn row to Soft.",
                kAppTitle,
                MB_OK | MB_ICONINFORMATION);
            return false;
        }

        return true;
    }

    const bool external = g_cutSubtitlesEnabled;
    const bool keepEmbedded = g_cutKeepEmbeddedSubtitles;
    if (!external && !keepEmbedded) return true;

    const bool multi = g_cutInputs.size() > 1;
    if (multi && (external || (keepEmbedded && CutterReencodesVideo()))) {
        MessageBoxW(g_main,
                    L"Subtitle management is currently limited to single-file jobs.\n\n"
                    L"Turn subtitles off for this multi-input job, or export/join first and add subtitles afterward.",
                    kAppTitle, MB_OK | MB_ICONINFORMATION);
        return false;
    }

    if (!external) return true;

    if (g_cutSubtitlePath.empty()) {
        MessageBoxW(g_main, L"Subtitles are enabled, but no subtitle file is selected.",
                    kAppTitle, MB_OK | MB_ICONWARNING);
        return false;
    }
    if (!FileExists(g_cutSubtitlePath)) {
        MessageBoxW(g_main, (L"Subtitle file not found:\n\n" + g_cutSubtitlePath).c_str(),
                    kAppTitle, MB_OK | MB_ICONWARNING);
        return false;
    }
    if (!CutterIsSubtitlePath(g_cutSubtitlePath)) {
        MessageBoxW(g_main, L"Subtitle file must be SRT, ASS, SSA, VTT, or SUB.",
                    kAppTitle, MB_OK | MB_ICONWARNING);
        return false;
    }
    std::error_code sizeError;
    const auto subtitleSize = std::filesystem::file_size(std::filesystem::path(g_cutSubtitlePath), sizeError);
    if (sizeError || subtitleSize == 0) {
        MessageBoxW(g_main, L"The selected subtitle file is empty or could not be read.",
                    kAppTitle, MB_OK | MB_ICONWARNING);
        return false;
    }

    if (jobMode == CutterOutputMode::ExtractAudio || jobMode == CutterOutputMode::M4A) {
        MessageBoxW(g_main, L"Subtitles do not apply to audio-only output modes.",
                    kAppTitle, MB_OK | MB_ICONINFORMATION);
        return false;
    }

    if (g_cutSubtitleMode == CutterSubtitleMode::Soft) {
        const std::wstring ext = CutterOutputExtension();
        if (_wcsicmp(ext.c_str(), L"mkv") != 0 && _wcsicmp(ext.c_str(), L"mp4") != 0) {
            MessageBoxW(g_main,
                        L"Soft subtitle tracks require MKV or MP4 output.\n\n"
                        L"Choose MKV/MP4, or switch Subtitles to Burn.",
                        kAppTitle, MB_OK | MB_ICONINFORMATION);
            return false;
        }
    } else if (!CutterReencodesVideo()) {
        MessageBoxW(g_main,
                    L"Burn-in subtitles require video re-encoding.\n\n"
                    L"Choose MP4 H.264 or WebM, or switch Subtitles to Soft.",
                    kAppTitle, MB_OK | MB_ICONINFORMATION);
        return false;
    }

    return true;
}

bool CutterParseTargetSizeField(double& valueKb, bool showError) {
    valueKb = 0.0;
    if (!g_cutSize) return true;

    wchar_t text[64]{};
    GetWindowTextW(g_cutSize, text, static_cast<int>(std::size(text)));
    std::wstring valueText(text);
    valueText.erase(std::remove_if(valueText.begin(), valueText.end(),
                                   [](wchar_t ch) { return iswspace(ch) != 0; }),
                    valueText.end());
    if (valueText.empty()) return true;

    // Accept either decimal separator in the compact toolbar field.  FFmpeg's
    // generated PowerShell still receives an invariant dot-decimal value.
    std::replace(valueText.begin(), valueText.end(), L',', L'.');
    wchar_t* end = nullptr;
    const double value = wcstod(valueText.c_str(), &end);
    const bool valid = end && end != valueText.c_str() && *end == L'\0' &&
                       std::isfinite(value) && value > 0.0 && value <= 100000000.0;
    if (!valid) {
        if (showError) {
            MessageBoxW(g_main,
                        L"Size KB must be a positive number, or left blank to use CRF.",
                        kAppTitle, MB_OK | MB_ICONWARNING);
        }
        return false;
    }

    valueKb = value;
    return true;
}

void CutterReadSizeField() {
    double parsed = 0.0;
    if (CutterParseTargetSizeField(parsed, false)) {
        g_cutTargetKb = parsed;
        SaveSettings();
    }
}

bool CutterParseCrfField(int& valueCrf, bool showError) {
    valueCrf = g_cutCrf;
    if (!g_cutCrfEdit) return true;

    wchar_t text[32]{};
    GetWindowTextW(g_cutCrfEdit, text, static_cast<int>(std::size(text)));
    std::wstring value(text);
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](wchar_t ch) { return iswspace(ch) != 0; }),
                value.end());

    wchar_t* end = nullptr;
    const long parsed = wcstol(value.c_str(), &end, 10);
    if (value.empty() || !end || end == value.c_str() || *end != L'\0' ||
        parsed < 0 || parsed > 51) {
        if (showError) {
            MessageBoxW(g_main, L"CRF must be a whole number from 0 to 51.",
                        kAppTitle, MB_OK | MB_ICONWARNING);
        }
        return false;
    }

    valueCrf = static_cast<int>(parsed);
    return true;
}

void CutterReadCrfField() {
    int parsed = g_cutCrf;
    if (CutterParseCrfField(parsed, false)) {
        g_cutCrf = parsed;
        SaveSettings();
    }
}

bool CutterParseAudioOffsetField(int& valueMs, bool showError) {
    valueMs = 0;
    if (!g_cutAudioOffset) return true;

    wchar_t text[64]{};
    GetWindowTextW(g_cutAudioOffset, text, static_cast<int>(std::size(text)));
    std::wstring value(text);
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](wchar_t ch) { return iswspace(ch) != 0; }),
                value.end());
    if (value.empty()) value = L"0";

    wchar_t* end = nullptr;
    const long parsed = wcstol(value.c_str(), &end, 10);
    if (!end || end == value.c_str() || *end != L'\0' ||
        parsed < -600000L || parsed > 600000L) {
        if (showError) {
            MessageBoxW(g_main,
                        L"Audio offset must be a whole number from -600000 to 600000 ms.",
                        kAppTitle, MB_OK | MB_ICONWARNING);
        }
        return false;
    }
    valueMs = static_cast<int>(parsed);
    return true;
}

void CutterReadAudioOffsetField() {
    int parsed = 0;
    if (!CutterParseAudioOffsetField(parsed, false)) return;

    if (auto* track = ActiveManagedAudioSyncTrack()) {
        track->offsetMs = parsed;
        CutterApplyAudioPreviewDelay();
    } else {
        g_cutAudioOffsetMs = parsed;
        SaveSettings();
    }
}

bool CutterParseAudioKbpsField(int& valueKbps, bool showError) {
    valueKbps = 0;
    if (g_cutAudioMode != CutterAudioMode::Auto) return true;

    if (!g_cutAudioKbpsEdit) {
        valueKbps = CutterEffectiveAudioKbps(g_cutMode, g_cutAudioKbps);
        return true;
    }

    wchar_t text[32]{};
    GetWindowTextW(g_cutAudioKbpsEdit, text, static_cast<int>(std::size(text)));
    std::wstring value(text);
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](wchar_t ch) { return iswspace(ch) != 0; }),
                value.end());

    if (value.empty()) {
        valueKbps = 128;
        return true;
    }

    wchar_t* end = nullptr;
    const long parsed = wcstol(value.c_str(), &end, 10);
    const bool valid = end && end != value.c_str() && *end == L'\0' &&
                       parsed >= 16 && parsed <= 512;
    if (!valid) {
        if (showError) {
            MessageBoxW(g_main,
                        L"Audio bitrate must be a whole number from 16 to 512 kbps.",
                        kAppTitle, MB_OK | MB_ICONWARNING);
        }
        return false;
    }

    valueKbps = static_cast<int>(parsed);
    return true;
}

void SyncCutterAudioControls(bool resetDefault = false) {
    if (g_cutAudioModeCombo) {
        SendMessageW(g_cutAudioModeCombo, CB_SETCURSEL,
                     static_cast<WPARAM>(static_cast<int>(g_cutAudioMode)), 0);
    }

    if (g_cutAudioKbpsEdit) {
        if (g_cutAudioMode == CutterAudioMode::Auto) {
            if (resetDefault || g_cutAudioKbps <= 0) g_cutAudioKbps = 128;
            SetWindowTextW(g_cutAudioKbpsEdit, std::to_wstring(g_cutAudioKbps).c_str());
            EnableWindow(g_cutAudioKbpsEdit, TRUE);
        } else {
            SetWindowTextW(g_cutAudioKbpsEdit, L"");
            EnableWindow(g_cutAudioKbpsEdit, FALSE);
        }
    }
}

void AdjustCutterAudioOffset(int deltaMs) {
    if (auto* track = ActiveManagedAudioSyncTrack()) {
        int current = track->offsetMs;
        CutterParseAudioOffsetField(current, false);
        track->offsetMs = std::clamp(current + deltaMs, -600000, 600000);
        if (g_cutAudioOffset) {
            SetWindowTextW(g_cutAudioOffset, std::to_wstring(track->offsetMs).c_str());
        }
        CutterApplyAudioPreviewDelay();
    } else {
        int current = g_cutAudioOffsetMs;
        CutterParseAudioOffsetField(current, false);
        g_cutAudioOffsetMs = std::clamp(current + deltaMs, -600000, 600000);
        if (g_cutAudioOffset) {
            SetWindowTextW(g_cutAudioOffset, std::to_wstring(g_cutAudioOffsetMs).c_str());
        }
        SaveSettings();
    }

    if (g_cutAudioSyncOpen) {
        CutterApplyAudioPreviewDelay();
    }

    if (g_cutAudioOffsetMinus) InvalidateRect(g_cutAudioOffsetMinus, nullptr, FALSE);
    if (g_cutAudioOffsetPlus) InvalidateRect(g_cutAudioOffsetPlus, nullptr, FALSE);
    if (g_cutAudioOffsetReset) InvalidateRect(g_cutAudioOffsetReset, nullptr, FALSE);
}

void SyncCutterControls() {
    if (g_cutFormat) {
        SendMessageW(g_cutFormat, CB_SETCURSEL,
                     static_cast<WPARAM>(static_cast<int>(g_cutMode)), 0);
    }
    if (g_cutResolution) {
        const wchar_t* values[] = { L"Source", L"1080p", L"720p", L"480p", L"360p" };
        int selected = 0;
        for (int i = 0; i < static_cast<int>(std::size(values)); ++i) {
            if (_wcsicmp(values[i], g_cutResolutionValue.c_str()) == 0) {
                selected = i;
                break;
            }
        }
        SendMessageW(g_cutResolution, CB_SETCURSEL, selected, 0);
    }
    if (g_cutSize) {
        if (g_cutTargetKb > 0.0) {
            wchar_t kb[32]{};
            swprintf_s(kb, L"%.0f", g_cutTargetKb);
            SetWindowTextW(g_cutSize, kb);
        } else {
            SetWindowTextW(g_cutSize, L"");
        }
        EnableWindow(g_cutSize, CutterSupportsTargetSize());
    }
    SyncCutterAudioControls(false);
    if (g_cutAudioTracksInitialized) {
        LoadManagedAudioSyncToControls();
    } else if (g_cutAudioOffset) {
        SetWindowTextW(g_cutAudioOffset, std::to_wstring(g_cutAudioOffsetMs).c_str());
    }
    if (g_cutSizeLabel) EnableWindow(g_cutSizeLabel, CutterSupportsTargetSize());
    if (g_cutTwoPassButton) {
        EnableWindow(g_cutTwoPassButton, CutterSupportsTargetSize());
        InvalidateRect(g_cutTwoPassButton, nullptr, FALSE);
    }
    if (g_cutCrfEdit) {
        SetWindowTextW(g_cutCrfEdit, std::to_wstring(g_cutCrf).c_str());
        EnableWindow(g_cutCrfEdit, CutterReencodesVideo());
    }
    if (g_cutCrfLabel) EnableWindow(g_cutCrfLabel, CutterReencodesVideo());
    if (g_cutResolution) EnableWindow(g_cutResolution, CutterReencodesVideo());
    if (g_cutResLabel) EnableWindow(g_cutResLabel, CutterReencodesVideo());

    if (g_cutOpen) InvalidateRect(g_cutOpen, nullptr, FALSE);
    if (g_cutMulti) InvalidateRect(g_cutMulti, nullptr, FALSE);
    if (g_cutTracks) InvalidateRect(g_cutTracks, nullptr, FALSE);
    if (g_cutAudioSyncButton) InvalidateRect(g_cutAudioSyncButton, nullptr, FALSE);
    if (g_cutMarkAButton) InvalidateRect(g_cutMarkAButton, nullptr, FALSE);
    if (g_cutMarkBButton) InvalidateRect(g_cutMarkBButton, nullptr, FALSE);
    if (g_cutCropButton) InvalidateRect(g_cutCropButton, nullptr, FALSE);
    if (g_cutSubtitles) InvalidateRect(g_cutSubtitles, nullptr, FALSE);
    if (g_cutOptions) InvalidateRect(g_cutOptions, nullptr, FALSE);
    if (g_cutTwoPassButton) InvalidateRect(g_cutTwoPassButton, nullptr, FALSE);
    if (g_cutNuke) InvalidateRect(g_cutNuke, nullptr, FALSE);
    if (g_cutConvert) InvalidateRect(g_cutConvert, nullptr, FALSE);
    if (g_seek) InvalidateRect(g_seek, nullptr, FALSE);
}

void ResetCutterSelection(bool clearInputs) {
    g_cutMarkA = -1.0;
    g_cutMarkB = -1.0;
    g_cutMarkBAutoEnd = false;
    g_cutCrop = {};
    g_cutVideoInfo = {};
    g_cutCropActive = false;
    g_cutCropSelecting = false;
    g_cutCropDragging = false;
    g_cutCropDragMode = CutterCropDragMode::None;
    g_cutOutputPath.clear();
    if (clearInputs) g_cutInputs.clear();
    if (g_overlay) {
        ReleaseCapture();
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        InvalidateRect(g_overlay, nullptr, TRUE);
    }
    if (g_cropOverlay) InvalidateRect(g_cropOverlay, nullptr, TRUE);
    SyncCutterControls();
}

void SetCutterMark(bool markA) {
    if (!g_mpvReady || g_currentMediaPath.empty()) {
        ShowOsdText(L"Open a file first", 1300);
        return;
    }
    const double value = std::max(0.0, MpvGetDouble("time-pos", g_timePos));
    if (markA) {
        g_cutMarkA = value;
        // A defines a new cut range. Until the user explicitly picks B, use
        // the end of the currently loaded file as the natural end marker.
        g_cutMarkBAutoEnd = true;
        if (std::isfinite(g_duration) && g_duration > value) {
            g_cutMarkB = g_duration;
        } else {
            g_cutMarkB = -1.0;
        }
    } else {
        g_cutMarkBAutoEnd = false;
        g_cutMarkB = value;
    }
    SyncCutterControls();
    if (markA && g_cutMarkB >= 0.0) {
        ShowOsdText(L"A  " + CutterTimestamp(value) + L"   B  END", 1900);
    } else {
        ShowOsdText(std::wstring(markA ? L"A  " : L"B  ") + CutterTimestamp(value), 1700);
    }
}

bool ReadCutterVideoInfo() {
    if (!g_mpvReady || !g_mpvApi.free_node_contents) return false;
    mpv_node vp{};
    if (g_mpvApi.get_property(g_mpv, "video-params", MPV_FORMAT_NODE, &vp) < 0) {
        return false;
    }

    CutterVideoInfo next{};
    if (vp.format == MPV_FORMAT_NODE_MAP) {
        next.w = static_cast<int>(NodeInt(MapValue(vp, "w"), 0));
        next.h = static_cast<int>(NodeInt(MapValue(vp, "h"), 0));
        next.dw = static_cast<int>(NodeInt(MapValue(vp, "dw"), next.w));
        next.dh = static_cast<int>(NodeInt(MapValue(vp, "dh"), next.h));
        next.rotate = static_cast<int>(NodeInt(MapValue(vp, "rotate"), 0));
        next.rotate %= 360;
        if (next.rotate < 0) next.rotate += 360;
        next.valid = next.w > 0 && next.h > 0;
    }
    g_mpvApi.free_node_contents(&vp);

    const bool geometryChanged = next.w != g_cutVideoInfo.w || next.h != g_cutVideoInfo.h;
    g_cutVideoInfo = next;
    if (g_cutVideoInfo.valid && (!g_cutCrop.valid || geometryChanged)) {
        g_cutCrop = { 0, 0, g_cutVideoInfo.w, g_cutVideoInfo.h, true };
    }
    return g_cutVideoInfo.valid;
}

RECT CutterVideoContentRect() {
    RECT client{};
    GetClientRect(g_video, &client);
    const int cw = std::max(1, static_cast<int>(client.right - client.left));
    const int ch = std::max(1, static_cast<int>(client.bottom - client.top));
    if (!g_cutVideoInfo.valid) return client;

    double dw = g_cutVideoInfo.dw > 0 ? static_cast<double>(g_cutVideoInfo.dw)
                                      : static_cast<double>(g_cutVideoInfo.w);
    double dh = g_cutVideoInfo.dh > 0 ? static_cast<double>(g_cutVideoInfo.dh)
                                      : static_cast<double>(g_cutVideoInfo.h);
    if (g_cutVideoInfo.rotate == 90 || g_cutVideoInfo.rotate == 270) std::swap(dw, dh);
    if (dw <= 0.0 || dh <= 0.0) return client;

    const double scale = std::min(static_cast<double>(cw) / dw,
                                  static_cast<double>(ch) / dh);
    const int outW = std::max(1, static_cast<int>(std::llround(dw * scale)));
    const int outH = std::max(1, static_cast<int>(std::llround(dh * scale)));
    const int left = (cw - outW) / 2;
    const int top = (ch - outH) / 2;
    return RECT{ left, top, left + outW, top + outH };
}

POINT CutterSourcePointFromOverlay(POINT p) {
    RECT vr = CutterVideoContentRect();
    const double vw = std::max(1L, vr.right - vr.left);
    const double vh = std::max(1L, vr.bottom - vr.top);
    double u = (static_cast<double>(p.x) - vr.left) / vw;
    double v = (static_cast<double>(p.y) - vr.top) / vh;
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);

    double su = u;
    double sv = v;
    switch (g_cutVideoInfo.rotate) {
    case 90:  su = v;       sv = 1.0 - u; break;
    case 180: su = 1.0 - u; sv = 1.0 - v; break;
    case 270: su = 1.0 - v; sv = u;       break;
    default: break;
    }

    const int sx = std::clamp(static_cast<int>(std::llround(su * g_cutVideoInfo.w)),
                              0, g_cutVideoInfo.w);
    const int sy = std::clamp(static_cast<int>(std::llround(sv * g_cutVideoInfo.h)),
                              0, g_cutVideoInfo.h);
    return POINT{ sx, sy };
}

POINT CutterOverlayPointFromSource(double sx, double sy) {
    RECT vr = CutterVideoContentRect();
    double su = g_cutVideoInfo.w > 0 ? sx / static_cast<double>(g_cutVideoInfo.w) : 0.0;
    double sv = g_cutVideoInfo.h > 0 ? sy / static_cast<double>(g_cutVideoInfo.h) : 0.0;
    su = std::clamp(su, 0.0, 1.0);
    sv = std::clamp(sv, 0.0, 1.0);

    double u = su;
    double v = sv;
    switch (g_cutVideoInfo.rotate) {
    case 90:  u = 1.0 - sv; v = su;       break;
    case 180: u = 1.0 - su; v = 1.0 - sv; break;
    case 270: u = sv;       v = 1.0 - su; break;
    default: break;
    }

    const double vw = std::max(1L, vr.right - vr.left);
    const double vh = std::max(1L, vr.bottom - vr.top);
    return POINT{
        static_cast<LONG>(std::llround(vr.left + u * vw)),
        static_cast<LONG>(std::llround(vr.top + v * vh))
    };
}

RECT CutterCropOverlayRect() {
    if (!g_cutCrop.valid || !g_cutVideoInfo.valid) return RECT{};
    const double x0 = static_cast<double>(g_cutCrop.x);
    const double y0 = static_cast<double>(g_cutCrop.y);
    const double x1 = static_cast<double>(g_cutCrop.x + g_cutCrop.w);
    const double y1 = static_cast<double>(g_cutCrop.y + g_cutCrop.h);
    POINT pts[4] = {
        CutterOverlayPointFromSource(x0, y0),
        CutterOverlayPointFromSource(x1, y0),
        CutterOverlayPointFromSource(x0, y1),
        CutterOverlayPointFromSource(x1, y1)
    };
    LONG left = pts[0].x, right = pts[0].x, top = pts[0].y, bottom = pts[0].y;
    for (const POINT& point : pts) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    return RECT{ left, top, right, bottom };
}

void SetCutterCropFromOverlayPoints(POINT a, POINT b) {
    if (!g_cutVideoInfo.valid) return;
    const POINT s0 = CutterSourcePointFromOverlay(a);
    const POINT s1 = CutterSourcePointFromOverlay(b);
    int left = std::min(s0.x, s1.x);
    int top = std::min(s0.y, s1.y);
    int right = std::max(s0.x, s1.x);
    int bottom = std::max(s0.y, s1.y);
    left = std::clamp(left, 0, g_cutVideoInfo.w - 1);
    top = std::clamp(top, 0, g_cutVideoInfo.h - 1);
    right = std::clamp(right, left + 1, g_cutVideoInfo.w);
    bottom = std::clamp(bottom, top + 1, g_cutVideoInfo.h);
    g_cutCrop = { left, top, right - left, bottom - top, true };
}

CutterCropDragMode HitTestCutterCrop(POINT p) {
    if (!g_cutCropActive || !g_cutCrop.valid || !g_cutVideoInfo.valid) {
        return CutterCropDragMode::None;
    }

    const RECT r = CutterCropOverlayRect();
    constexpr int kHandleRadius = 8;
    auto nearX = [&](LONG x) { return std::abs(p.x - x) <= kHandleRadius; };
    auto nearY = [&](LONG y) { return std::abs(p.y - y) <= kHandleRadius; };
    const bool betweenX = p.x >= r.left - kHandleRadius && p.x <= r.right + kHandleRadius;
    const bool betweenY = p.y >= r.top - kHandleRadius && p.y <= r.bottom + kHandleRadius;

    if (nearX(r.left) && nearY(r.top)) return CutterCropDragMode::TopLeft;
    if (nearX(r.right) && nearY(r.top)) return CutterCropDragMode::TopRight;
    if (nearX(r.left) && nearY(r.bottom)) return CutterCropDragMode::BottomLeft;
    if (nearX(r.right) && nearY(r.bottom)) return CutterCropDragMode::BottomRight;
    if (nearX(r.left) && betweenY) return CutterCropDragMode::Left;
    if (nearX(r.right) && betweenY) return CutterCropDragMode::Right;
    if (nearY(r.top) && betweenX) return CutterCropDragMode::Top;
    if (nearY(r.bottom) && betweenX) return CutterCropDragMode::Bottom;

    RECT inside = r;
    if (PtInRect(&inside, p)) return CutterCropDragMode::Move;
    return CutterCropDragMode::None;
}

HCURSOR CutterCropCursor(CutterCropDragMode mode) {
    switch (mode) {
    case CutterCropDragMode::Left:
    case CutterCropDragMode::Right:
        return LoadCursorW(nullptr, IDC_SIZEWE);
    case CutterCropDragMode::Top:
    case CutterCropDragMode::Bottom:
        return LoadCursorW(nullptr, IDC_SIZENS);
    case CutterCropDragMode::TopLeft:
    case CutterCropDragMode::BottomRight:
        return LoadCursorW(nullptr, IDC_SIZENWSE);
    case CutterCropDragMode::TopRight:
    case CutterCropDragMode::BottomLeft:
        return LoadCursorW(nullptr, IDC_SIZENESW);
    case CutterCropDragMode::Move:
        return LoadCursorW(nullptr, IDC_SIZEALL);
    default:
        return LoadCursorW(nullptr, IDC_CROSS);
    }
}

void UpdateCutterCropResize(POINT p) {
    if (!g_cutCropDragging ||
        g_cutCropDragMode == CutterCropDragMode::None ||
        g_cutCropDragMode == CutterCropDragMode::NewRect) {
        return;
    }

    RECT bounds = CutterVideoContentRect();
    RECT r = g_cutCropDragOriginRect;
    const int dx = p.x - g_cutCropDragAnchor.x;
    const int dy = p.y - g_cutCropDragAnchor.y;
    constexpr int kMinPixels = 4;

    auto clampLeft = [&](LONG value) {
        return std::clamp<LONG>(value, bounds.left, r.right - kMinPixels);
    };
    auto clampRight = [&](LONG value) {
        return std::clamp<LONG>(value, r.left + kMinPixels, bounds.right);
    };
    auto clampTop = [&](LONG value) {
        return std::clamp<LONG>(value, bounds.top, r.bottom - kMinPixels);
    };
    auto clampBottom = [&](LONG value) {
        return std::clamp<LONG>(value, r.top + kMinPixels, bounds.bottom);
    };

    switch (g_cutCropDragMode) {
    case CutterCropDragMode::Move: {
        const LONG width = r.right - r.left;
        const LONG height = r.bottom - r.top;
        LONG left = g_cutCropDragOriginRect.left + dx;
        LONG top = g_cutCropDragOriginRect.top + dy;
        left = std::clamp<LONG>(left, bounds.left, std::max(bounds.left, bounds.right - width));
        top = std::clamp<LONG>(top, bounds.top, std::max(bounds.top, bounds.bottom - height));
        r = RECT{ left, top, left + width, top + height };
        break;
    }
    case CutterCropDragMode::Left:
        r.left = clampLeft(g_cutCropDragOriginRect.left + dx);
        break;
    case CutterCropDragMode::Right:
        r.right = clampRight(g_cutCropDragOriginRect.right + dx);
        break;
    case CutterCropDragMode::Top:
        r.top = clampTop(g_cutCropDragOriginRect.top + dy);
        break;
    case CutterCropDragMode::Bottom:
        r.bottom = clampBottom(g_cutCropDragOriginRect.bottom + dy);
        break;
    case CutterCropDragMode::TopLeft:
        r.left = clampLeft(g_cutCropDragOriginRect.left + dx);
        r.top = clampTop(g_cutCropDragOriginRect.top + dy);
        break;
    case CutterCropDragMode::TopRight:
        r.right = clampRight(g_cutCropDragOriginRect.right + dx);
        r.top = clampTop(g_cutCropDragOriginRect.top + dy);
        break;
    case CutterCropDragMode::BottomLeft:
        r.left = clampLeft(g_cutCropDragOriginRect.left + dx);
        r.bottom = clampBottom(g_cutCropDragOriginRect.bottom + dy);
        break;
    case CutterCropDragMode::BottomRight:
        r.right = clampRight(g_cutCropDragOriginRect.right + dx);
        r.bottom = clampBottom(g_cutCropDragOriginRect.bottom + dy);
        break;
    default:
        break;
    }

    SetCutterCropFromOverlayPoints(POINT{ r.left, r.top },
                                   POINT{ r.right, r.bottom });
    g_cutCropActive = g_cutCrop.valid;
}

void DisableCutterCrop() {
    if (GetCapture()) ReleaseCapture();

    g_cutCropActive = false;
    g_cutCropSelecting = false;
    g_cutCropDragging = false;
    g_cutCropDragMode = CutterCropDragMode::None;

    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    if (g_cropOverlay) {
        InvalidateRect(g_cropOverlay, nullptr, TRUE);
    }

    SyncCutterControls();
}

void BeginCutterCropSelection() {
    if (g_cutCropActive || g_cutCropSelecting) {
        DisableCutterCrop();
        ShowOsdText(L"Crop: OFF", 1200);
        return;
    }

    if (g_currentMediaPath.empty() || !ReadCutterVideoInfo()) {
        ShowOsdText(L"Open a video first", 1400);
        return;
    }
    if (!CutterReencodesVideo()) {
        g_cutMode = CutterOutputMode::Mp4H264;
        SyncCutterControls();
        ShowOsdText(L"Crop requires re-encode; switched to MP4 H.264", 2200);
    }
    MpvSetFlag("pause", true);
    g_cutCropActive = true;
    g_cutCropSelecting = true;
    g_cutCropDragging = false;
    g_cutCropDragMode = CutterCropDragMode::None;
    SetCursor(CutterCropCursor(g_cutCrop.valid ? CutterCropDragMode::Move
                                              : CutterCropDragMode::None));
    if (g_cropOverlay) InvalidateRect(g_cropOverlay, nullptr, TRUE);
    SyncCutterControls();
    ShowOsdText(g_cutCrop.valid
                    ? L"Crop: ON - drag edges/corners to resize; drag inside to move"
                    : L"Crop: ON - drag a rectangle",
                3000);
}

std::wstring FindCutterTool(const wchar_t* name) {
    const std::wstring exeDir = GetExeDirectory();
    const std::wstring tool(name);
    const std::wstring local1 = exeDir + L"\\ffmpeg\\" + tool;
    const std::wstring local2 = exeDir + L"\\ffmpeg\\bin\\" + tool;
    const std::wstring msys = L"C:\\msys64\\mingw64\\bin\\" + tool;
    const std::wstring candidates[] = { local1, local2, msys };
    for (const auto& path : candidates) {
        if (FileExists(path)) return path;
    }

    wchar_t resolved[32768]{};
    const DWORD n = SearchPathW(nullptr, name, nullptr,
                                static_cast<DWORD>(std::size(resolved)), resolved, nullptr);
    if (n > 0 && n < std::size(resolved)) return std::wstring(resolved, n);
    return {};
}

std::wstring PowerShellQuote(const std::wstring& value) {
    std::wstring out = L"'";
    for (wchar_t ch : value) {
        if (ch == L'\'') out += L"''";
        else out.push_back(ch);
    }
    out.push_back(L'\'');
    return out;
}

std::wstring CutterTempPath(const wchar_t* suffix) {
    wchar_t temp[MAX_PATH + 2]{};
    DWORD n = GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
    std::wstring dir = (n > 0 && n < std::size(temp)) ? std::wstring(temp, n) : GetExeDirectory() + L"\\";
    // GetTickCount64 alone can repeat when several subtitle temp files are
    // created inside the same millisecond. A process-local serial keeps every
    // extraction/rewrite/progress path distinct without changing cleanup.
    static unsigned long long serial = 0;
    ++serial;
    return dir + L"WinterStatic-Cutter-" + std::to_wstring(GetCurrentProcessId()) +
           L"-" + std::to_wstring(GetTickCount64()) + L"-" +
           std::to_wstring(serial) + suffix;
}

bool WriteUtf8BomFile(const std::wstring& path, const std::wstring& text) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    file.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    const std::string utf8 = WideToUtf8(text);
    file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return static_cast<bool>(file);
}

bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    const std::string utf8 = WideToUtf8(text);
    file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return static_cast<bool>(file);
}

std::wstring FfconcatEscape(std::wstring path) {
    std::replace(path.begin(), path.end(), L'\\', L'/');
    std::wstring out;
    out.reserve(path.size() + 8);
    for (wchar_t ch : path) {
        // FFmpeg concat quoting follows its own quoting rules. A literal single
        // quote must close the quoted span, be escaped, then reopen it.
        if (ch == L'\'') out += L"'\\''";
        else out.push_back(ch);
    }
    return out;
}

void CleanupCutterSyncedSubtitleTemps() {
    for (const auto& path : g_cutSyncedSubtitlePaths) {
        if (!path.empty()) DeleteFileW(path.c_str());
    }
    g_cutSyncedSubtitlePaths.clear();
}

void CleanupCutterWorkerTemps() {
    if (!g_cutWorkerScriptPath.empty()) DeleteFileW(g_cutWorkerScriptPath.c_str());
    if (!g_cutConcatPath.empty()) DeleteFileW(g_cutConcatPath.c_str());
    if (!g_cutProgressPath.empty()) DeleteFileW(g_cutProgressPath.c_str());
    if (!g_cutProgressPhasePath.empty()) DeleteFileW(g_cutProgressPhasePath.c_str());
    if (!g_cutDonePath.empty()) DeleteFileW(g_cutDonePath.c_str());

    CleanupCutterSyncedSubtitleTemps();

    g_cutWorkerScriptPath.clear();
    g_cutConcatPath.clear();
    g_cutProgressPath.clear();
    g_cutProgressPhasePath.clear();
    g_cutDonePath.clear();
}

void CleanupCutterWorker() {
    if (g_cutWorkerProcess) {
        CloseHandle(g_cutWorkerProcess);
        g_cutWorkerProcess = nullptr;
    }
    if (g_cutWorkerJob) {
        CloseHandle(g_cutWorkerJob);
        g_cutWorkerJob = nullptr;
    }
    g_cutWorkerPid = 0;

    CleanupCutterWorkerTemps();

    g_cutWorkerKeepConsoleOpen = false;
    g_cutProgressIndeterminate = false;
    if (g_cutProgress) InvalidateRect(g_cutProgress, nullptr, FALSE);
    if (g_cutNuke) InvalidateRect(g_cutNuke, nullptr, FALSE);
    if (g_cutConvert) InvalidateRect(g_cutConvert, nullptr, FALSE);
}

void CleanupLingeringCutterConsoles() {
    for (HANDLE job : g_cutLingeringJobs) {
        if (job) CloseHandle(job);
    }
    g_cutLingeringJobs.clear();
}

int ReadCutterDoneStatus() {
    if (g_cutDonePath.empty()) return -1;
    std::ifstream file(std::filesystem::path(g_cutDonePath), std::ios::binary);
    if (!file) return -1;
    int code = -1;
    file >> code;
    return code;
}

double ReadCutterProgressSeconds() {
    if (g_cutProgressPath.empty()) return -1.0;
    std::ifstream file(std::filesystem::path(g_cutProgressPath), std::ios::binary);
    if (!file) return -1.0;
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::string key = "out_time=";
    const size_t pos = text.rfind(key);
    if (pos == std::string::npos) return -1.0;
    const size_t begin = pos + key.size();
    const size_t end = text.find_first_of("\r\n", begin);
    const std::string value = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    int h = 0, m = 0;
    double sec = 0.0;
    if (sscanf_s(value.c_str(), "%d:%d:%lf", &h, &m, &sec) != 3) return -1.0;
    return std::max(0.0, h * 3600.0 + m * 60.0 + sec);
}

int ReadCutterProgressPhase() {
    if (g_cutProgressPhasePath.empty()) return 1;
    std::ifstream file(std::filesystem::path(g_cutProgressPhasePath), std::ios::binary);
    if (!file) return 1;
    int phase = 1;
    file >> phase;
    return std::clamp(phase, 1, std::max(1, g_cutProgressPasses));
}

void UpdateCutterProgress() {
    if (!g_cutWorkerProcess) return;
    ++g_cutProgressPulse;
    if (g_cutProgressDurationSeconds <= 0.0) {
        g_cutProgressIndeterminate = true;
        if (g_cutProgress) InvalidateRect(g_cutProgress, nullptr, FALSE);
        return;
    }
    const double seconds = ReadCutterProgressSeconds();
    if (seconds < 0.0) return;
    const int phase = ReadCutterProgressPhase();
    const double local = std::clamp(seconds / g_cutProgressDurationSeconds, 0.0, 1.0);
    g_cutProgressFraction = std::clamp(((phase - 1) + local) / std::max(1, g_cutProgressPasses), 0.0, 1.0);
    g_cutProgressIndeterminate = false;
    if (g_cutProgress) InvalidateRect(g_cutProgress, nullptr, FALSE);
}

void PollCutterWorker() {
    if (!g_cutWorkerProcess) return;
    UpdateCutterProgress();

    // If the user asked to keep the visible PowerShell console open, the worker
    // writes a completion marker before waiting for Enter.  Treat the conversion
    // as finished at that point so another job can be started while the old
    // console remains available for inspection.
    if (g_cutWorkerKeepConsoleOpen) {
        const int doneStatus = ReadCutterDoneStatus();
        if (doneStatus >= 0) {
            g_cutProgressFraction = doneStatus == 0 ? 1.0 : 0.0;
            g_cutProgressIndeterminate = false;

            if (g_cutWorkerProcess) {
                CloseHandle(g_cutWorkerProcess);
                g_cutWorkerProcess = nullptr;
            }
            if (g_cutWorkerJob) {
                g_cutLingeringJobs.push_back(g_cutWorkerJob);
                g_cutWorkerJob = nullptr;
            }
            g_cutWorkerPid = 0;

            CleanupCutterWorkerTemps();
            g_cutWorkerKeepConsoleOpen = false;

            if (g_cutProgress) InvalidateRect(g_cutProgress, nullptr, FALSE);
            if (g_cutNuke) InvalidateRect(g_cutNuke, nullptr, FALSE);
            if (g_cutConvert) InvalidateRect(g_cutConvert, nullptr, FALSE);
            ShowOsdText(doneStatus == 0 ? L"Convert finished - PowerShell left open"
                                        : L"Convert stopped / failed - PowerShell left open",
                        2600);
            return;
        }
    }

    if (WaitForSingleObject(g_cutWorkerProcess, 0) != WAIT_OBJECT_0) return;
    DWORD exitCode = 0;
    GetExitCodeProcess(g_cutWorkerProcess, &exitCode);
    if (exitCode == 0) g_cutProgressFraction = 1.0;
    else g_cutProgressFraction = 0.0;
    CleanupCutterWorker();
    if (g_cutProgress) InvalidateRect(g_cutProgress, nullptr, FALSE);
    ShowOsdText(exitCode == 0 ? L"Convert finished" : L"Convert stopped / failed", 2200);
}

void ClearCutterInputs() {
    if (g_mpvReady) {
        MpvCommand({"stop"});
        MpvCommand({"playlist-clear"});
    }
    g_currentMediaPath.clear();
    ResetKeyframeIndexCache();
    g_timePos = 0.0;
    g_duration = 0.0;
    ResetCutterSelection(true);
    ResetCutterManagedAudioTracksForSource();
    ResetCutterManagedSubtitleTracksForSource();
    g_cutSubtitlesEnabled = false;
    g_cutSubtitlePath.clear();
    g_cutSubtitleSyncASet = false;
    g_cutSubtitleSyncBSet = false;
    g_cutSubtitleDriftEnabled = false;
    SyncCutterSubtitleContextControls();
    if (g_cutSubtitles) InvalidateRect(g_cutSubtitles, nullptr, FALSE);
    SaveSettings();
    SetMediaWindowTitle(L"");
    ShowOsdText(L"Inputs cleared", 1200);
}

void NukeCutter() {
    if (g_cutWorkerJob) {
        TerminateJobObject(g_cutWorkerJob, 1);
        g_cutProgressFraction = 0.0;
        CleanupCutterWorker();
        if (g_cutProgress) InvalidateRect(g_cutProgress, nullptr, FALSE);
        ShowOsdText(L"Conversion processes killed", 1300);
    } else {
        ShowOsdText(L"No conversion process is running", 1100);
    }
}

std::wstring CutterSuggestedOutputPath(bool makeUnique) {
    const std::wstring source = !g_cutInputs.empty()
        ? g_cutInputs.front() : g_currentMediaPath;
    if (source.empty()) return L"";

    const std::filesystem::path src(source);
    const std::wstring ext = CutterOutputExtension();
    const bool multi = g_cutInputs.size() > 1;
    std::wstring suffix = multi ? L"_joined" : L"_cut";
    if (g_cutMode == CutterOutputMode::M4A) suffix = L"_aac";
    if (g_cutMode == CutterOutputMode::ExtractAudio) suffix = L"_audio";

    const std::filesystem::path directory =
        src.parent_path().empty() ? std::filesystem::path(L".") : src.parent_path();
    const std::wstring base = src.stem().wstring() + suffix;
    std::filesystem::path candidate = directory / (base + L"." + ext);

    if (!makeUnique || !FileExists(candidate.wstring())) {
        return candidate.wstring();
    }

    for (int i = 2; i < 10000; ++i) {
        candidate = directory / (base + L"_" + std::to_wstring(i) + L"." + ext);
        if (!FileExists(candidate.wstring())) return candidate.wstring();
    }
    return L"";
}

bool EnsureCutterOutputPath() {
    if (!g_cutOutputPath.empty()) return true;
    g_cutOutputPath = CutterSuggestedOutputPath(true);
    if (g_cutOutputPath.empty()) {
        MessageBoxW(g_main, L"Could not create an automatic output filename.",
                    kAppTitle, MB_OK | MB_ICONERROR);
        return false;
    }
    ShowOsdText(L"Output: " + std::filesystem::path(g_cutOutputPath).filename().wstring(),
                1500);
    return true;
}

bool ChooseCutterOutput() {
    const std::wstring source = !g_cutInputs.empty()
        ? g_cutInputs.front() : g_currentMediaPath;
    if (source.empty()) {
        MessageBoxW(g_main, L"Open an input file first.", kAppTitle, MB_OK | MB_ICONINFORMATION);
        return false;
    }

    const std::wstring ext = CutterOutputExtension();
    const std::wstring suggested = CutterSuggestedOutputPath(false);

    std::vector<wchar_t> file(32768, L'\0');
    wcsncpy_s(file.data(), file.size(), suggested.c_str(), _TRUNCATE);
    std::wstring filterLabel = ext + L" files (*." + ext + L")";
    std::wstring filter = filterLabel + L'\0' + L"*." + ext + L'\0' + L"All files" + L'\0' + L"*.*" + L'\0' + L'\0';

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.lpstrDefExt = ext.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return false;
    g_cutOutputPath = file.data();
    ShowOsdText(L"Output: " + std::filesystem::path(g_cutOutputPath).filename().wstring(), 1800);
    return !g_cutOutputPath.empty();
}

std::wstring CutterVideoFilter(const std::wstring& burnSubtitlePath = L"") {
    std::wstring filter;
    if (g_cutCropActive && g_cutCrop.valid && g_cutVideoInfo.valid) {
        filter = L"crop=" + std::to_wstring(g_cutCrop.w) + L":" +
                 std::to_wstring(g_cutCrop.h) + L":" +
                 std::to_wstring(g_cutCrop.x) + L":" +
                 std::to_wstring(g_cutCrop.y) + L":exact=1";
    }

    int height = 0;
    if (_wcsicmp(g_cutResolutionValue.c_str(), L"1080p") == 0) height = 1080;
    else if (_wcsicmp(g_cutResolutionValue.c_str(), L"720p") == 0) height = 720;
    else if (_wcsicmp(g_cutResolutionValue.c_str(), L"480p") == 0) height = 480;
    else if (_wcsicmp(g_cutResolutionValue.c_str(), L"360p") == 0) height = 360;

    if (height > 0) {
        if (!filter.empty()) filter += L",";
        filter += L"scale=-2:" + std::to_wstring(height);
    } else if (g_cutCropActive) {
        if (!filter.empty()) filter += L",";
        filter += L"pad=ceil(iw/2)*2:ceil(ih/2)*2:0:0:black";
    }

    // Burn-in is deliberately last so subtitle placement is based on the final
    // cropped/scaled frame geometry, not the source geometry.
    if (!burnSubtitlePath.empty()) {
        if (!filter.empty()) filter += L",";
        filter += L"subtitles='" + CutterSubtitleFilterPath(burnSubtitlePath) + L"'";
    }
    return filter;
}

std::wstring CutterAudioFilterForOffset(int offsetMs, bool padForVideo) {
    if (offsetMs == 0) return L"";

    if (offsetMs > 0) {
        return L"adelay=" + std::to_wstring(offsetMs) + L":all=1";
    }

    const double seconds = static_cast<double>(-offsetMs) / 1000.0;
    wchar_t amount[64]{};
    swprintf_s(amount, L"%.6f", seconds);
    std::wstring filter =
        L"atrim=start=" + std::wstring(amount) + L",asetpts=PTS-STARTPTS";
    if (padForVideo) filter += L",apad";
    return filter;
}

bool BuildCutterConcatFile() {
    if (g_cutInputs.size() <= 1) return true;
    g_cutConcatPath = CutterTempPath(L".ffconcat");
    std::wstring body = L"ffconcat version 1.0\r\n";
    for (const auto& raw : g_cutInputs) {
        const std::wstring absolute = AbsoluteLocalPath(raw);
        body += L"file '" + FfconcatEscape(absolute) + L"'\r\n";
    }
    return WriteUtf8File(g_cutConcatPath, body);
}

bool StartCutterConvert() {
    // Snapshot the compact toolbar settings for each job so the visible Size KB
    // value and cached numeric value cannot diverge across successive media loads.
    double targetKb = 0.0;
    if (CutterSupportsTargetSize()) {
        if (!CutterParseTargetSizeField(targetKb, true)) return false;
        g_cutTargetKb = targetKb;
    }

    // CRF is only active for a re-encode job when no target size is set.
    // Disabled/inactive toolbar fields should never block an unrelated copy
    // or target-size job just because an old persisted value is malformed.
    int parsedCrf = g_cutCrf;
    if (CutterReencodesVideo() && targetKb <= 0.0) {
        if (!CutterParseCrfField(parsedCrf, true)) return false;
        g_cutCrf = parsedCrf;
    }
    const double targetMb = targetKb > 0.0 ? targetKb / 1024.0 : 0.0;
    const CutterOutputMode jobMode = g_cutMode;
    const int jobCrf = g_cutCrf;
    const CutterAudioMode jobAudioMode = g_cutAudioMode;
    int jobAudioKbps = 0;
    if (jobMode == CutterOutputMode::M4A) {
        // M4A is explicitly an AAC re-encode mode; video Audio Copy/None does not apply.
        if (g_cutAudioKbpsEdit) {
            wchar_t text[32]{};
            GetWindowTextW(g_cutAudioKbpsEdit, text, static_cast<int>(std::size(text)));
            std::wstring value(text);
            value.erase(std::remove_if(value.begin(), value.end(),
                                       [](wchar_t ch) { return iswspace(ch) != 0; }),
                        value.end());
            if (value.empty()) {
                jobAudioKbps = 128;
            } else {
                wchar_t* end = nullptr;
                const long parsed = wcstol(value.c_str(), &end, 10);
                if (!end || end == value.c_str() || *end != L'\0' || parsed < 16 || parsed > 512) {
                    MessageBoxW(g_main, L"Audio bitrate must be a whole number from 16 to 512 kbps.",
                                kAppTitle, MB_OK | MB_ICONWARNING);
                    return false;
                }
                jobAudioKbps = static_cast<int>(parsed);
            }
        } else {
            jobAudioKbps = 128;
        }
    } else {
        if (!CutterParseAudioKbpsField(jobAudioKbps, true)) return false;
        if (jobAudioMode == CutterAudioMode::Auto) g_cutAudioKbps = jobAudioKbps;
    }
    int jobAudioOffsetMs = g_cutAudioOffsetMs;
    if (g_cutAudioTracksInitialized) {
        if (g_cutAudioSyncOpen &&
            !CutterCommitAudioSyncOffsetEdit(true)) {
            return false;
        }
        // Managed audio owns timing per track. The legacy global value is used
        // only by old/unmanaged MULTI paths.
        jobAudioOffsetMs = 0;
    } else if (g_cutAudioOffset) {
        if (!CutterParseAudioOffsetField(jobAudioOffsetMs, true)) return false;
        g_cutAudioOffsetMs = jobAudioOffsetMs;
    }
    const bool showPowerShell = g_cutShowPowerShell;
    const bool closePowerShellWhenDone = !showPowerShell || g_cutClosePowerShellWhenDone;
    const bool overwriteOutput = g_cutOverwriteEnabled;
    const bool jobTwoPass = g_cutTwoPass;
    const bool jobSubtitlesEnabled = g_cutSubtitlesEnabled;
    const CutterSubtitleMode jobSubtitleMode = g_cutSubtitleMode;
    const std::wstring jobSubtitlePath = g_cutSubtitlePath;
    const std::wstring jobSubtitleTitle = g_cutSubtitleTitle;
    const bool jobKeepEmbeddedSubtitles = g_cutKeepEmbeddedSubtitles;
    int jobSubtitleOffsetMs = g_cutSubtitleOffsetMs;
    if (g_cutSubOffsetEdit) {
        if (!CutterParseSubtitleOffsetText(jobSubtitleOffsetMs, true)) return false;
        g_cutSubtitleOffsetMs = jobSubtitleOffsetMs;
    }
    const bool jobSubtitleSyncASet = g_cutSubtitleSyncASet;
    const bool jobSubtitleSyncBSet = g_cutSubtitleSyncBSet;
    const double jobSubtitleSyncAVideoSeconds = g_cutSubtitleSyncAVideoSeconds;
    const double jobSubtitleSyncBVideoSeconds = g_cutSubtitleSyncBVideoSeconds;
    const int jobSubtitleSyncAOffsetMs = g_cutSubtitleSyncAOffsetMs;
    const int jobSubtitleSyncBOffsetMs = g_cutSubtitleSyncBOffsetMs;
    const bool jobSubtitleDriftEnabled =
        g_cutSubtitleDriftEnabled &&
        jobSubtitleSyncASet &&
        jobSubtitleSyncBSet &&
        CutterSubtitleDriftReady();
    if (g_cutWorkerProcess) {
        MessageBoxW(g_main, L"A conversion is already running. Use NUKE to stop it first.",
                    kAppTitle, MB_OK | MB_ICONINFORMATION);
        return false;
    }

    CleanupCutterSyncedSubtitleTemps();
    if (g_cutInputs.empty() && !g_currentMediaPath.empty()) {
        g_cutInputs.push_back(g_currentMediaPath);
    }
    if (g_cutInputs.empty()) {
        MessageBoxW(g_main, L"Open an input file first.", kAppTitle, MB_OK | MB_ICONINFORMATION);
        return false;
    }
    EnsureCutterManagedAudioSource();
    EnsureCutterManagedSubtitleSource();

    // Single-file Cutter jobs always use the managed audio model now. This
    // makes the clip-friendly default (one mpv-selected track IN, the rest OUT)
    // authoritative even if TRACKS was never opened explicitly.
    if (g_cutInputs.size() <= 1) {
        EnsureCutterAudioTracksInitialized();
        if (g_cutAudioSyncOpen &&
            !CutterCommitAudioSyncOffsetEdit(true)) {
            return false;
        }
    }

    if (!CutterValidateManagedAudioTracks(jobMode)) return false;
    if (!CutterValidateSubtitles(jobMode)) return false;
    if (!EnsureCutterOutputPath()) return false;

    const std::wstring ffmpeg = FindCutterTool(L"ffmpeg.exe");
    if (ffmpeg.empty()) {
        MessageBoxW(g_main,
                    L"ffmpeg.exe was not found.\n\nChecked a local ffmpeg folder, C:\\msys64\\mingw64\\bin, and PATH.",
                    kAppTitle, MB_OK | MB_ICONERROR);
        return false;
    }
    const std::wstring ffprobe = FindCutterTool(L"ffprobe.exe");

    // Copy-mode MULTI intentionally does not gate conversion on an automatic
    // ffprobe compatibility check. FFmpeg's concat demuxer is authoritative;
    // a probe failure should not prevent the user from attempting the join.

    if (!BuildCutterConcatFile()) {
        MessageBoxW(g_main, L"Could not create the temporary multi-input concat file.",
                    kAppTitle, MB_OK | MB_ICONERROR);
        return false;
    }

    const bool multi = g_cutInputs.size() > 1;
    const double start = !multi && g_cutMarkA >= 0.0 ? g_cutMarkA : 0.0;
    const double end = !multi && g_cutMarkB >= 0.0 ? g_cutMarkB : g_duration;
    if (!multi && g_cutMarkB >= 0.0 && g_cutMarkB <= start) {
        MessageBoxW(g_main, L"B must be later than A.", kAppTitle, MB_OK | MB_ICONWARNING);
        return false;
    }
    if (!multi && end <= start && g_duration > 0.0) {
        MessageBoxW(g_main, L"The selected A/B range is empty.", kAppTitle, MB_OK | MB_ICONWARNING);
        return false;
    }

    const bool useManagedAudio = g_cutAudioTracksInitialized && !multi;
    const auto jobAudioTracks = g_cutAudioTracks;

    if (!multi) {
        // mpv playlist/auto-next changes can replace the primary source without
        // passing through LoadPlaylist(). Capture the current external sync
        // state, then invalidate source-derived embedded rows if necessary.
        if (g_cutSubtitleTracksInitialized) {
            StoreLegacySubtitleSyncToManaged();
        }
        EnsureCutterManagedSubtitleSource();

        // If the user prepared external subtitles before opening the video, the
        // source load intentionally left those rows pending while the embedded
        // stream list was unavailable. Resolve the embedded rows now before the
        // job snapshot even if SUBS OPT was not reopened after loading the file.
        if (!g_cutSubtitleTracksInitialized &&
            !g_cutSubtitleTracks.empty()) {
            EnsureCutterSubtitleTracksInitialized();
            if (CutterHasActiveSubtitleSyncTarget()) {
                LoadManagedSubtitleSyncToLegacy();
            }
        }
    }

    if (g_cutSubtitleTracksInitialized) {
        StoreLegacySubtitleSyncToManaged();
    }
    const bool useManagedSubtitles =
        g_cutSubtitleTracksInitialized && !multi;
    const auto jobSubtitleTracks = g_cutSubtitleTracks;

    const bool subtitleSoft = !useManagedSubtitles &&
                              jobSubtitlesEnabled &&
                              jobSubtitleMode == CutterSubtitleMode::Soft;
    const bool subtitleBurn = !useManagedSubtitles &&
                              jobSubtitlesEnabled &&
                              jobSubtitleMode == CutterSubtitleMode::Burn;

    std::wstring jobSubtitleTimingPath = jobSubtitlePath;
    const bool subtitleTimingRewriteNeeded =
        !useManagedSubtitles &&
        jobSubtitlesEnabled &&
        (jobSubtitleOffsetMs != 0 ||
         jobSubtitleDriftEnabled ||
         (subtitleBurn && start > 0.0005));

    if (subtitleTimingRewriteNeeded) {
        CutterSubtitleSyncSpec syncSpec{};
        syncSpec.constantOffsetMs = jobSubtitleOffsetMs;
        syncSpec.driftEnabled = jobSubtitleDriftEnabled;
        syncSpec.videoASeconds = jobSubtitleSyncAVideoSeconds;
        syncSpec.videoBSeconds = jobSubtitleSyncBVideoSeconds;
        syncSpec.offsetAMs = jobSubtitleSyncAOffsetMs;
        syncSpec.offsetBMs = jobSubtitleSyncBOffsetMs;
        // Soft subtitle inputs are separately seeked to A below. Burned
        // subtitles are read by a filter, so make their rewritten cue times
        // relative to the trimmed output timeline here.
        syncSpec.timelineShiftMs = subtitleBurn
            ? static_cast<int>(std::llround(start * 1000.0))
            : 0;


        CleanupCutterSyncedSubtitleTemps();

        std::wstring rewrittenPath;
        std::wstring rewriteError;
        if (!CutterRewriteSubtitleTiming(
                jobSubtitlePath,
                syncSpec,
                CutterCurrentVideoFps(),
                rewrittenPath,
                rewriteError)) {
            MessageBoxW(
                g_main,
                rewriteError.c_str(),
                kAppTitle,
                MB_OK | MB_ICONWARNING);
            return false;
        }
        jobSubtitleTimingPath = rewrittenPath;
        g_cutSyncedSubtitlePaths.push_back(rewrittenPath);
    }

    std::vector<std::wstring> managedSubtitleTimingPaths(
        jobSubtitleTracks.size());
    std::vector<bool> managedSubtitleUsesInputFile(
        jobSubtitleTracks.size(), false);
    int managedBurnSubtitleIndex = -1;

    if (useManagedSubtitles) {
        for (size_t i = 0; i < jobSubtitleTracks.size(); ++i) {
            const auto& track = jobSubtitleTracks[i];
            if (!track.included) continue;

            if (track.mode == CutterSubtitleMode::Burn &&
                !track.embedded) {
                managedBurnSubtitleIndex = static_cast<int>(i);
            }

            const bool driftReady =
                track.driftEnabled &&
                CutterManagedSubtitleDriftReady(track);

            if (track.embedded) {
                // Zero-correction fast path: keep mapping the original embedded
                // stream directly. Extraction is only needed when the timing
                // actually changes.
                const bool rewriteNeeded =
                    track.offsetMs != 0 || driftReady;
                if (!rewriteNeeded) continue;

                if (!track.syncCapable || track.ffmpegStreamIndex < 0) {
                    CleanupCutterSyncedSubtitleTemps();
                    MessageBoxW(
                        g_main,
                        L"The selected embedded subtitle cannot be identified safely for timing correction.",
                        kAppTitle,
                        MB_OK | MB_ICONWARNING);
                    return false;
                }

                if (ffmpeg.empty()) {
                    CleanupCutterSyncedSubtitleTemps();
                    MessageBoxW(
                        g_main,
                        L"ffmpeg.exe is required to extract an embedded subtitle for timing correction.",
                        kAppTitle,
                        MB_OK | MB_ICONWARNING);
                    return false;
                }

                const std::wstring extractBase =
                    CutterTempPath(L".embedded-subtitle");
                winterstatic::subtitle::ExtractOptions extractOptions;
                extractOptions.tools.ffmpeg =
                    std::filesystem::path(ffmpeg);
                if (!ffprobe.empty()) {
                    extractOptions.tools.ffprobe =
                        std::filesystem::path(ffprobe);
                }
                extractOptions.overwrite = true;

                const std::string sourceCodec =
                    WideToUtf8(track.codec);
                if (sourceCodec.empty()) {
                    CleanupCutterSyncedSubtitleTemps();
                    MessageBoxW(
                        g_main,
                        L"Subtitle codec information is unavailable; format-preserving extraction cannot continue.",
                        kAppTitle,
                        MB_OK | MB_ICONWARNING);
                    return false;
                }

                const auto extractResult =
                    winterstatic::subtitle::extract_subtitle_text(
                        std::filesystem::path(
                            AbsoluteLocalPath(g_cutInputs.front())),
                        track.ffmpegStreamIndex,
                        sourceCodec,
                        std::filesystem::path(extractBase),
                        extractOptions);
                if (!extractResult.success) {
                    CleanupCutterSyncedSubtitleTemps();
                    const std::wstring details =
                        Utf8ToWide(extractResult.message);
                    MessageBoxW(
                        g_main,
                        (L"Could not extract the selected embedded subtitle for timing correction.\n\n" +
                         details).c_str(),
                        kAppTitle,
                        MB_OK | MB_ICONWARNING);
                    return false;
                }

                const std::wstring extractPath =
                    extractResult.output_path.wstring();
                g_cutSyncedSubtitlePaths.push_back(extractPath);

                CutterSubtitleSyncSpec syncSpec{};
                syncSpec.constantOffsetMs = track.offsetMs;
                syncSpec.driftEnabled = driftReady;
                syncSpec.videoASeconds = track.syncAVideoSeconds;
                syncSpec.videoBSeconds = track.syncBVideoSeconds;
                syncSpec.offsetAMs = track.syncAOffsetMs;
                syncSpec.offsetBMs = track.syncBOffsetMs;
                // Corrected embedded Soft subtitles become temporary external
                // subtitle inputs and are seeked to A below, just like ordinary
                // external Soft subtitles.
                syncSpec.timelineShiftMs = 0;

                std::wstring rewrittenPath;
                std::wstring rewriteError;
                if (!CutterRewriteSubtitleTiming(
                        extractPath,
                        syncSpec,
                        CutterCurrentVideoFps(),
                        rewrittenPath,
                        rewriteError)) {
                    CleanupCutterSyncedSubtitleTemps();
                    MessageBoxW(
                        g_main,
                        rewriteError.c_str(),
                        kAppTitle,
                        MB_OK | MB_ICONWARNING);
                    return false;
                }

                managedSubtitleTimingPaths[i] = rewrittenPath;
                managedSubtitleUsesInputFile[i] = true;
                g_cutSyncedSubtitlePaths.push_back(rewrittenPath);
                continue;
            }

            std::wstring timingPath = track.externalPath;
            const bool rewriteNeeded =
                track.offsetMs != 0 ||
                driftReady ||
                (track.mode == CutterSubtitleMode::Burn &&
                 start > 0.0005);

            if (rewriteNeeded) {
                CutterSubtitleSyncSpec syncSpec{};
                syncSpec.constantOffsetMs = track.offsetMs;
                syncSpec.driftEnabled = driftReady;
                syncSpec.videoASeconds = track.syncAVideoSeconds;
                syncSpec.videoBSeconds = track.syncBVideoSeconds;
                syncSpec.offsetAMs = track.syncAOffsetMs;
                syncSpec.offsetBMs = track.syncBOffsetMs;
                syncSpec.timelineShiftMs =
                    track.mode == CutterSubtitleMode::Burn
                        ? static_cast<int>(
                              std::llround(start * 1000.0))
                        : 0;

                std::wstring rewrittenPath;
                std::wstring rewriteError;
                if (!CutterRewriteSubtitleTiming(
                        track.externalPath,
                        syncSpec,
                        CutterCurrentVideoFps(),
                        rewrittenPath,
                        rewriteError)) {
                    CleanupCutterSyncedSubtitleTemps();
                    MessageBoxW(
                        g_main,
                        rewriteError.c_str(),
                        kAppTitle,
                        MB_OK | MB_ICONWARNING);
                    return false;
                }

                timingPath = rewrittenPath;
                g_cutSyncedSubtitlePaths.push_back(
                    rewrittenPath);
            }

            managedSubtitleTimingPaths[i] =
                std::move(timingPath);
            managedSubtitleUsesInputFile[i] =
                track.mode == CutterSubtitleMode::Soft;
        }
    }

    const int videoInputIndex = 0;
    int nextInputIndex = 1;

    // External audio rows become real FFmpeg inputs. Keep each assigned input
    // index beside its manager row so output mapping can follow the user's list
    // order instead of input order.
    std::vector<int> managedAudioInputIndices(jobAudioTracks.size(), -1);
    if (useManagedAudio) {
        for (size_t i = 0; i < jobAudioTracks.size(); ++i) {
            const auto& track = jobAudioTracks[i];
            if (track.included && !track.embedded) {
                managedAudioInputIndices[i] = nextInputIndex++;
            }
        }
    }

    std::vector<int> managedSubtitleInputIndices(
        jobSubtitleTracks.size(), -1);
    if (useManagedSubtitles) {
        for (size_t i = 0; i < jobSubtitleTracks.size(); ++i) {
            const auto& track = jobSubtitleTracks[i];
            if (track.included &&
                track.mode == CutterSubtitleMode::Soft &&
                managedSubtitleUsesInputFile[i]) {
                managedSubtitleInputIndices[i] = nextInputIndex++;
            }
        }
    }

    const int subtitleInputIndex =
        (!useManagedSubtitles && subtitleSoft)
            ? nextInputIndex++
            : -1;
    const std::wstring outputExtension = CutterOutputExtension();
    const bool subtitleOutputMp4 =
        _wcsicmp(outputExtension.c_str(), L"mp4") == 0;
    const bool keepEmbeddedForEncode =
        !useManagedSubtitles &&
        jobKeepEmbeddedSubtitles &&
        subtitleOutputMp4;
    const std::wstring videoMap =
        std::to_wstring(videoInputIndex) + L":v:0";
    const std::wstring audioMap =
        std::to_wstring(videoInputIndex) + L":a?";
    const std::wstring sourceVideoAllMap =
        std::to_wstring(videoInputIndex) + L":v?";
    const std::wstring sourceAudioAllMap =
        std::to_wstring(videoInputIndex) + L":a?";
    const std::wstring sourceSubtitleAllMap =
        std::to_wstring(videoInputIndex) + L":s?";
    const std::wstring externalSubtitleMap = subtitleSoft
        ? std::to_wstring(subtitleInputIndex) + L":s:0?"
        : L"";
    const std::wstring subtitleCodec =
        subtitleOutputMp4 ? L"mov_text" : L"copy";

    std::vector<std::wstring> managedAudioMaps;
    if (useManagedAudio) {
        for (size_t i = 0; i < jobAudioTracks.size(); ++i) {
            const auto& track = jobAudioTracks[i];
            if (!track.included) continue;

            if (track.embedded) {
                managedAudioMaps.push_back(
                    std::to_wstring(videoInputIndex) + L":a:" +
                    std::to_wstring(track.streamIndex) + L"?");
            } else if (managedAudioInputIndices[i] >= 0) {
                managedAudioMaps.push_back(
                    std::to_wstring(managedAudioInputIndices[i]) + L":a:0?");
            }
        }
    }

    const int managedIncludedAudioCount =
        static_cast<int>(managedAudioMaps.size());

    std::vector<std::wstring> managedSubtitleMaps;
    if (useManagedSubtitles) {
        for (size_t i = 0; i < jobSubtitleTracks.size(); ++i) {
            const auto& track = jobSubtitleTracks[i];
            if (!track.included ||
                track.mode != CutterSubtitleMode::Soft) {
                continue;
            }

            if (managedSubtitleUsesInputFile[i] &&
                managedSubtitleInputIndices[i] >= 0) {
                managedSubtitleMaps.push_back(
                    std::to_wstring(managedSubtitleInputIndices[i]) +
                    L":s:0?");
            } else if (track.embedded) {
                if (track.ffmpegStreamIndex >= 0) {
                    managedSubtitleMaps.push_back(
                        std::to_wstring(videoInputIndex) + L":" +
                        std::to_wstring(track.ffmpegStreamIndex) + L"?");
                } else {
                    managedSubtitleMaps.push_back(
                        std::to_wstring(videoInputIndex) + L":s:" +
                        std::to_wstring(track.streamIndex) + L"?");
                }
            }
        }
    }

    std::wstring managedBurnSubtitlePath;
    if (useManagedSubtitles &&
        managedBurnSubtitleIndex >= 0 &&
        managedBurnSubtitleIndex <
            static_cast<int>(managedSubtitleTimingPaths.size())) {
        managedBurnSubtitlePath =
            managedSubtitleTimingPaths[
                static_cast<size_t>(managedBurnSubtitleIndex)];
    }

    (void)nextInputIndex;

    const bool managedAudioHasOffset =
        useManagedAudio &&
        std::any_of(
            jobAudioTracks.begin(),
            jobAudioTracks.end(),
            [](const CutterManagedAudioTrack& track) {
                return track.included && track.offsetMs != 0;
            });
    const bool anyAudioOffset =
        managedAudioHasOffset || (!useManagedAudio && jobAudioOffsetMs != 0);

    if (anyAudioOffset &&
        (jobMode == CutterOutputMode::Source ||
         jobMode == CutterOutputMode::Mkv ||
         jobMode == CutterOutputMode::Mp4Copy ||
         jobMode == CutterOutputMode::ExtractAudio ||
         (CutterReencodesVideo() && jobAudioMode == CutterAudioMode::Copy))) {
        MessageBoxW(g_main,
                    L"Audio offset requires an audio re-encode.\n\n"
                    L"Use Audio = Auto for MP4 H.264/WebM, use Audio to M4A, or reset the affected audio track offset to 0.",
                    kAppTitle, MB_OK | MB_ICONINFORMATION);

        CleanupCutterSyncedSubtitleTemps();
        return false;
    }

    g_cutProgressDurationSeconds = multi ? 0.0 : std::max(0.0, end - start);
    if (g_cutProgressDurationSeconds <= 0.0 && !multi) g_cutProgressDurationSeconds = g_duration;
    g_cutProgressPasses =
        (CutterReencodesVideo() && targetMb > 0.0 && jobTwoPass) ? 2 : 1;
    g_cutProgressFraction = 0.0;
    g_cutProgressPulse = 0;
    g_cutProgressIndeterminate = g_cutProgressDurationSeconds <= 0.0;
    g_cutProgressPath = CutterTempPath(L".progress.txt");
    g_cutProgressPhasePath = CutterTempPath(L".phase.txt");
    g_cutDonePath = CutterTempPath(L".done.txt");
    g_cutWorkerKeepConsoleOpen = showPowerShell && !closePowerShellWhenDone;
    if (g_cutProgress) InvalidateRect(g_cutProgress, nullptr, FALSE);

    std::wstring script;
    script += L"$ErrorActionPreference = 'Stop'\r\n";
    script += L"$ffmpeg = " + PowerShellQuote(ffmpeg) + L"\r\n";
    if (!ffprobe.empty()) script += L"$ffprobe = " + PowerShellQuote(ffprobe) + L"\r\n";
    script += L"$output = " + PowerShellQuote(g_cutOutputPath) + L"\r\n";
    script += L"$progressPath = " + PowerShellQuote(g_cutProgressPath) + L"\r\n";
    script += L"$phasePath = " + PowerShellQuote(g_cutProgressPhasePath) + L"\r\n";
    script += L"$donePath = " + PowerShellQuote(g_cutDonePath) + L"\r\n";
    script += std::wstring(L"$closeWhenDone = ") + (closePowerShellWhenDone ? L"$true" : L"$false") + L"\r\n";
    script += L"trap { Set-Content -LiteralPath $donePath -Value '1' -NoNewline; Write-Host ''; Write-Host ('FAILED: ' + $_.Exception.Message) -ForegroundColor Red; if (-not $closeWhenDone) { [void](Read-Host 'Press Enter to close') }; exit 1 }\r\n";
    script += L"$runIndex = 0\r\n";
    script += L"$inputArgs = @()\r\n";
    if (multi && !ffprobe.empty()) {
        script += L"$probeInputs = @(";
        for (size_t i = 0; i < g_cutInputs.size(); ++i) {
            if (i) script += L",";
            script += PowerShellQuote(AbsoluteLocalPath(g_cutInputs[i]));
        }
        script += L")\r\n";
    }
    script += L"Write-Host 'WinterStatic Video Cutter' -ForegroundColor Cyan\r\n";
    script += L"Write-Host 'Mode: " + std::wstring(CutterModeText(jobMode)) + L"'\r\n";
    script += L"Write-Host 'Resolution: " + g_cutResolutionValue + L"'\r\n";
    if (targetKb > 0.0) {
        wchar_t targetBanner[64]{};
        swprintf_s(targetBanner, L"%.0f", targetKb);
        script += L"Write-Host 'Target size: " + std::wstring(targetBanner) +
                  (jobTwoPass ? L" KB (2-pass)'" : L" KB (1-pass)'") +
                  L" -ForegroundColor Yellow\r\n";
    } else {
        script += L"Write-Host 'Target size: off; CRF " + std::to_wstring(jobCrf) + L"'\r\n";
    }
    if (CutterReencodesVideo()) {
        std::wstring audioBanner = CutterAudioModeText(jobAudioMode);
        if (jobAudioMode == CutterAudioMode::Auto) {
            audioBanner += L" (" + std::to_wstring(jobAudioKbps) + L" kbps)";
        }
        script += L"Write-Host 'Audio: " + audioBanner +
                  (useManagedAudio
                       ? std::wstring(L"; per-track offsets")
                       : L"; offset " + std::to_wstring(jobAudioOffsetMs) + L" ms") +
                  L"'\r\n";
    } else if (jobMode == CutterOutputMode::M4A) {
        script += L"Write-Host 'Audio: AAC " + std::to_wstring(jobAudioKbps) +
                  (useManagedAudio
                       ? std::wstring(L" kbps; per-track offsets")
                       : L" kbps; offset " + std::to_wstring(jobAudioOffsetMs) + L" ms") +
                  L"'\r\n";
    } else {
        script += L"Write-Host 'Audio: stream copy where applicable" +
                  (useManagedAudio
                       ? std::wstring(L"; per-track offsets")
                       : L"; offset " + std::to_wstring(jobAudioOffsetMs) + L" ms") +
                  L"'\r\n";
    }
    if (useManagedAudio) {
        script += L"Write-Host 'TRACKS: " +
                  std::to_wstring(managedIncludedAudioCount) +
                  L" audio track(s) included' -ForegroundColor DarkGray\r\n";
    }
    script += std::wstring(L"Write-Host 'Overwrite: ") + (overwriteOutput ? L"on" : L"off") + L"'\r\n";
    if (useManagedSubtitles) {
        int managedSubtitleIncluded = 0;
        int managedSubtitleSoftCount = 0;
        int managedSubtitleBurnCount = 0;
        for (const auto& track : jobSubtitleTracks) {
            if (!track.included) continue;
            ++managedSubtitleIncluded;
            if (track.mode == CutterSubtitleMode::Burn) ++managedSubtitleBurnCount;
            else ++managedSubtitleSoftCount;
        }
        script += L"Write-Host 'SUBS OPT: " +
                  std::to_wstring(managedSubtitleIncluded) +
                  L" track(s) included; " +
                  std::to_wstring(managedSubtitleSoftCount) +
                  L" soft; " +
                  std::to_wstring(managedSubtitleBurnCount) +
                  L" burn' -ForegroundColor DarkGray\r\n";
    } else if (subtitleSoft) {
        script += L"Write-Host 'Subtitles: soft track - " +
                  std::filesystem::path(jobSubtitlePath).filename().wstring() +
                  L"; offset " + std::to_wstring(jobSubtitleOffsetMs) + L" ms" +
                  (jobSubtitleDriftEnabled ? L"; drift correction ON" : L"") +
                  L"'\r\n";
    } else if (subtitleBurn) {
        script += L"Write-Host 'Subtitles: burn-in - " +
                  std::filesystem::path(jobSubtitlePath).filename().wstring() +
                  L"; offset " + std::to_wstring(jobSubtitleOffsetMs) + L" ms" +
                  (jobSubtitleDriftEnabled ? L"; drift correction ON" : L"") +
                  L"'\r\n";
    } else if (jobKeepEmbeddedSubtitles) {
        script += L"Write-Host 'Subtitles: preserve embedded tracks where supported'\r\n";
    }
    script += L"Write-Host ''\r\n";

    if (multi &&
        (jobMode == CutterOutputMode::Source ||
         jobMode == CutterOutputMode::Mkv ||
         jobMode == CutterOutputMode::Mp4Copy)) {
        script += L"Write-Host 'Multi-source copy join: letting FFmpeg attempt concat directly.' -ForegroundColor Cyan\r\n";
        script += L"Write-Host 'If the streams are incompatible, FFmpeg will report the actual failure below.' -ForegroundColor DarkGray\r\n\r\n";
    }

    if (multi) {
        script += L"Write-Host ('Concat list: ' + " + PowerShellQuote(g_cutConcatPath) + L") -ForegroundColor DarkGray\r\n";
        script += L"$inputArgs += @('-f','concat','-safe','0','-i'," + PowerShellQuote(g_cutConcatPath) + L")\r\n";
    } else {
        if (start > 0.0005) {
            script += L"$inputArgs += @('-accurate_seek','-ss','" + CutterTimestamp(start) + L"')\r\n";
        }
        script += L"$inputArgs += @('-i'," + PowerShellQuote(AbsoluteLocalPath(g_cutInputs.front())) + L")\r\n";

        if (useManagedAudio) {
            for (size_t i = 0; i < jobAudioTracks.size(); ++i) {
                const auto& track = jobAudioTracks[i];
                if (!track.included || track.embedded ||
                    managedAudioInputIndices[i] < 0) {
                    continue;
                }

                if (start > 0.0005) {
                    script += L"$inputArgs += @('-ss','" +
                              CutterTimestamp(start) + L"')\r\n";
                }
                script += L"$inputArgs += @('-i'," +
                          PowerShellQuote(
                              AbsoluteLocalPath(track.externalPath)) +
                          L")\r\n";
            }
        }

        if (useManagedSubtitles) {
            for (size_t i = 0; i < jobSubtitleTracks.size(); ++i) {
                const auto& track = jobSubtitleTracks[i];
                if (!track.included ||
                    track.mode != CutterSubtitleMode::Soft ||
                    !managedSubtitleUsesInputFile[i] ||
                    managedSubtitleInputIndices[i] < 0) {
                    continue;
                }

                if (start > 0.0005) {
                    script += L"$inputArgs += @('-ss','" +
                              CutterTimestamp(start) + L"')\r\n";
                }
                script += L"$inputArgs += @('-i'," +
                          PowerShellQuote(
                              AbsoluteLocalPath(
                                  managedSubtitleTimingPaths[i])) +
                          L")\r\n";
            }
        } else if (subtitleSoft) {
            if (start > 0.0005) {
                script += L"$inputArgs += @('-ss','" + CutterTimestamp(start) + L"')\r\n";
            }
            script += L"$inputArgs += @('-i'," + PowerShellQuote(AbsoluteLocalPath(jobSubtitleTimingPath)) + L")\r\n";
        }
        if (end > start && g_duration > 0.0) {
            wchar_t dur[64]{};
            swprintf_s(dur, L"%.6f", end - start);
            script += L"$inputArgs += @('-t','" + std::wstring(dur) + L"')\r\n";
        }
    }

    script += L"function Run-FF([object[]]$a) { $script:runIndex++; Set-Content -LiteralPath $phasePath -Value $script:runIndex -NoNewline; Remove-Item -LiteralPath $progressPath -Force -ErrorAction SilentlyContinue; & $ffmpeg '-progress' $progressPath '-stats_period' '0.25' @a; if ($LASTEXITCODE -ne 0) { throw \"FFmpeg exited with code $LASTEXITCODE\" } }\r\n";
    script += L"$common = @('-hide_banner') + $inputArgs\r\n";
    script += std::wstring(L"$overwriteArgs = @('") +
              (overwriteOutput ? L"-y" : L"-n") + L"')\r\n";
    script += L"$audioMapArgs = @()\r\n";
    script += L"$audioOnlyMapArgs = @()\r\n";
    if (useManagedAudio) {
        for (const auto& map : managedAudioMaps) {
            script += L"$audioMapArgs += @('-map','" + map + L"')\r\n";
            script += L"$audioOnlyMapArgs += @('-map','" + map + L"')\r\n";
        }
    } else {
        script += L"$audioMapArgs += @('-map','" + audioMap + L"')\r\n";
        // Unless TRACKS has been explicitly committed, audio-only output keeps
        // the simple default of using the first source audio stream.
        script += L"$audioOnlyMapArgs += @('-map','0:a:0?')\r\n";
    }

    script += L"$audioVideoFilterArgs = @()\r\n";
    script += L"$audioOnlyFilterArgs = @()\r\n";
    if (useManagedAudio) {
        int outputAudioIndex = 0;
        for (const auto& track : jobAudioTracks) {
            if (!track.included) continue;

            const std::wstring videoFilter =
                CutterAudioFilterForOffset(track.offsetMs, true);
            const std::wstring audioOnlyFilter =
                CutterAudioFilterForOffset(track.offsetMs, false);
            if (!videoFilter.empty()) {
                script += L"$audioVideoFilterArgs += @('-filter:a:" +
                          std::to_wstring(outputAudioIndex) + L"'," +
                          PowerShellQuote(videoFilter) + L")\r\n";
            }
            if (!audioOnlyFilter.empty()) {
                script += L"$audioOnlyFilterArgs += @('-filter:a:" +
                          std::to_wstring(outputAudioIndex) + L"'," +
                          PowerShellQuote(audioOnlyFilter) + L")\r\n";
            }
            ++outputAudioIndex;
        }
    }

    script += L"$subtitleMapArgs = @()\r\n";
    script += L"$subtitleCodecArgs = @()\r\n";
    if (useManagedSubtitles) {
        for (const auto& map : managedSubtitleMaps) {
            script += L"$subtitleMapArgs += @('-map','" + map + L"')\r\n";
        }
        if (!managedSubtitleMaps.empty()) {
            script += L"$subtitleCodecArgs += @('-c:s','" +
                      subtitleCodec + L"')\r\n";
        }

        int softOutputIndex = 0;
        for (const auto& track : jobSubtitleTracks) {
            if (!track.included ||
                track.mode != CutterSubtitleMode::Soft) {
                continue;
            }
            if (!track.title.empty()) {
                script += L"$subtitleCodecArgs += @('-metadata:s:s:" +
                          std::to_wstring(softOutputIndex) +
                          L"'," +
                          PowerShellQuote(L"title=" + track.title) +
                          L")\r\n";
            }
            if (!track.language.empty()) {
                script += L"$subtitleCodecArgs += @('-metadata:s:s:" +
                          std::to_wstring(softOutputIndex) +
                          L"'," +
                          PowerShellQuote(L"language=" + track.language) +
                          L")\r\n";
            }
            ++softOutputIndex;
        }
    } else {
        if (subtitleSoft) {
            script += L"$subtitleMapArgs += @('-map','" +
                      externalSubtitleMap + L"')\r\n";
        }
        if (keepEmbeddedForEncode) {
            script += L"$subtitleMapArgs += @('-map','" +
                      sourceSubtitleAllMap + L"')\r\n";
        }
        if (subtitleSoft || keepEmbeddedForEncode) {
            script += L"$subtitleCodecArgs += @('-c:s','" +
                      subtitleCodec + L"')\r\n";
        }
        if (subtitleSoft && !jobSubtitleTitle.empty()) {
            script += L"$subtitleCodecArgs += @('-metadata:s:s:0'," +
                      PowerShellQuote(
                          L"title=" + jobSubtitleTitle) +
                      L")\r\n";
        }
    }

    const std::wstring burnSubtitlePath =
        useManagedSubtitles
            ? managedBurnSubtitlePath
            : (subtitleBurn ? jobSubtitleTimingPath : L"");
    const std::wstring vf =
        CutterVideoFilter(burnSubtitlePath);
    const std::wstring afVideo =
        useManagedAudio ? L"" : CutterAudioFilterForOffset(jobAudioOffsetMs, true);
    const std::wstring afAudioOnly =
        useManagedAudio ? L"" : CutterAudioFilterForOffset(jobAudioOffsetMs, false);
    const bool encode = CutterReencodesVideo();

    if (g_cutMode == CutterOutputMode::Source ||
        g_cutMode == CutterOutputMode::Mkv ||
        g_cutMode == CutterOutputMode::Mp4Copy) {
        if (useManagedAudio || useManagedSubtitles) {
            // Once either manager is active, make stream selection/order explicit
            // while still preserving source video/data/attachments/metadata.
            script += L"$a = $common + $overwriteArgs + @('-map','" +
                      sourceVideoAllMap + L"')\r\n";

            if (useManagedAudio) {
                script += L"$a += $audioMapArgs\r\n";
            } else {
                script += L"$a += @('-map','" +
                          sourceAudioAllMap + L"')\r\n";
            }

            if (useManagedSubtitles) {
                script += L"$a += $subtitleMapArgs\r\n";
            } else if (subtitleSoft) {
                script += L"$a += $subtitleMapArgs + @('-map','" +
                          sourceSubtitleAllMap + L"')\r\n";
            } else {
                script += L"$a += @('-map','" +
                          sourceSubtitleAllMap + L"')\r\n";
            }

            script += L"$a += @('-map','0:d?','-map','0:t?','-map_metadata','0','-map_chapters','0','-c','copy')\r\n";

            if ((useManagedSubtitles && !managedSubtitleMaps.empty()) ||
                (!useManagedSubtitles && subtitleSoft)) {
                script += L"$a += $subtitleCodecArgs\r\n";
            }
        } else if (subtitleSoft) {
            // Keep the source streams, but place the external subtitle before
            // any embedded subtitle streams so metadata:s:s:0 reliably refers
            // to the newly-added track.
            script += L"$a = $common + $overwriteArgs + @('-map','" + sourceVideoAllMap +
                      L"','-map','" + sourceAudioAllMap + L"','-map','" + externalSubtitleMap +
                      L"','-map','" + sourceSubtitleAllMap +
                      L"','-map','0:d?','-map','0:t?','-map_metadata','0','-map_chapters','0','-c','copy','-c:s','" +
                      subtitleCodec + L"')\r\n";
            if (!jobSubtitleTitle.empty()) {
                script += L"$a += @('-metadata:s:s:0'," +
                          PowerShellQuote(L"title=" + jobSubtitleTitle) + L")\r\n";
            }
        } else {
            script += L"$a = $common + $overwriteArgs + @('-map','0','-c','copy')\r\n";
        }
        if (g_cutMode == CutterOutputMode::Mp4Copy) {
            script += L"$a += @('-movflags','+faststart')\r\n";
        }
        script += L"$a += @($output)\r\nRun-FF $a\r\n";
    } else if (g_cutMode == CutterOutputMode::ExtractAudio) {
        script += L"$a = $common + $overwriteArgs + @('-vn') + $audioOnlyMapArgs + @('-c:a','copy',$output)\r\nRun-FF $a\r\n";
    } else if (jobMode == CutterOutputMode::M4A) {
        script += L"$a = $common + $overwriteArgs + @('-vn') + $audioOnlyMapArgs\r\n";
        if (useManagedAudio && managedAudioHasOffset) {
            script += L"$a += $audioOnlyFilterArgs\r\n";
        } else if (!afAudioOnly.empty()) {
            script += L"$a += @('-af'," + PowerShellQuote(afAudioOnly) + L")\r\n";
        }
        script += L"$a += @('-c:a','aac','-b:a','" + std::to_wstring(jobAudioKbps) + L"k',$output)\r\nRun-FF $a\r\n";
    } else if (encode) {
        const bool audioNone =
            jobAudioMode == CutterAudioMode::None ||
            (useManagedAudio && managedAudioMaps.empty());
        const bool audioCopy = jobAudioMode == CutterAudioMode::Copy;
        const int audioTrackCountForBudget = useManagedAudio
            ? std::max(1, managedIncludedAudioCount)
            : 1;
        const int audioK = (audioNone || audioCopy)
            ? 0
            : jobAudioKbps * audioTrackCountForBudget;
        script += L"$duration = 0.0\r\n";
        if (multi) {
            if (ffprobe.empty() && targetMb > 0.0) {
                MessageBoxW(g_main, L"ffprobe.exe is required for target-size encoding with multiple inputs.",
                            kAppTitle, MB_OK | MB_ICONERROR);
                return false;
            }
            if (!ffprobe.empty()) {
                script += L"foreach ($p in $probeInputs) { $d = & $ffprobe '-v' 'error' '-show_entries' 'format=duration' '-of' 'default=nw=1:nk=1' $p; if ($LASTEXITCODE -ne 0) { throw 'ffprobe failed' }; $duration += [double]::Parse(($d | Select-Object -First 1), [Globalization.CultureInfo]::InvariantCulture) }\r\n";
            }
        } else {
            const double duration = end > start ? end - start : g_duration;
            wchar_t durationText[64]{};
            swprintf_s(durationText, L"%.6f", std::max(0.001, duration));
            script += L"$duration = " + std::wstring(durationText) + L"\r\n";
        }

        if (targetMb > 0.0) {
            wchar_t mb[64]{};
            swprintf_s(mb, L"%.6f", targetMb);
            script += L"$targetMb = " + std::wstring(mb) + L"\r\n";
            script += L"if ($duration -le 0) { throw 'Could not determine output duration.' }\r\n";
            script += L"$videoK = [Math]::Floor((($targetMb * 1024 * 1024 * 8 * 0.985) / $duration / 1000) - " + std::to_wstring(audioK) + L")\r\n";
            script += L"if ($videoK -lt 100) { throw 'Target size is too small for this duration.' }\r\n";
            if (jobTwoPass) {
                script += L"$passlog = [IO.Path]::Combine([IO.Path]::GetTempPath(), 'WinterStatic-pass-' + [Guid]::NewGuid().ToString('N'))\r\n";
                script += L"try {\r\n";
                script += L"  $p1 = $common + @('-y','-map','" + videoMap + L"')\r\n";
                if (!vf.empty()) script += L"  $p1 += @('-vf'," + PowerShellQuote(vf) + L")\r\n";
                if (g_cutMode == CutterOutputMode::Mp4H264) {
                    script += L"  $p1 += @('-c:v','libx264','-preset','medium','-b:v',(\"${videoK}k\"),'-pass','1','-passlogfile',$passlog,'-an','-f','null','NUL')\r\n";
                } else {
                    script += L"  $p1 += @('-c:v','libvpx-vp9','-deadline','good','-cpu-used','2','-b:v',(\"${videoK}k\"),'-pass','1','-passlogfile',$passlog,'-an','-f','null','NUL')\r\n";
                }
                script += L"  Run-FF $p1\r\n";
                if (audioNone) {
                    script += L"  $p2 = $common + $overwriteArgs + @('-map','" + videoMap + L"','-an') + $subtitleMapArgs\r\n";
                } else {
                    script += L"  $p2 = $common + $overwriteArgs + @('-map','" + videoMap + L"') + $audioMapArgs + $subtitleMapArgs\r\n";
                }
                if (!vf.empty()) script += L"  $p2 += @('-vf'," + PowerShellQuote(vf) + L")\r\n";
                if (useManagedAudio && managedAudioHasOffset && !audioCopy && !audioNone) {
                    script += L"  $p2 += $audioVideoFilterArgs + @('-shortest')\r\n";
                } else if (!afVideo.empty() && !audioNone) {
                    script += L"  $p2 += @('-af'," + PowerShellQuote(afVideo) + L",'-shortest')\r\n";
                }
                if (g_cutMode == CutterOutputMode::Mp4H264) {
                    script += L"  $p2 += @('-c:v','libx264','-preset','medium','-b:v',(\"${videoK}k\"),'-pass','2','-passlogfile',$passlog,'-pix_fmt','yuv420p')\r\n";
                    if (audioCopy) script += L"  $p2 += @('-c:a','copy')\r\n";
                    else if (!audioNone) script += L"  $p2 += @('-c:a','aac','-b:a','" + std::to_wstring(jobAudioKbps) + L"k')\r\n";
                    script += L"  $p2 += $subtitleCodecArgs\r\n";
                    script += L"  $p2 += @('-movflags','+faststart',$output)\r\n";
                } else {
                    script += L"  $p2 += @('-c:v','libvpx-vp9','-deadline','good','-cpu-used','2','-b:v',(\"${videoK}k\"),'-pass','2','-passlogfile',$passlog)\r\n";
                    if (audioCopy) script += L"  $p2 += @('-c:a','copy')\r\n";
                    else if (!audioNone) script += L"  $p2 += @('-c:a','libopus','-b:a','" + std::to_wstring(jobAudioKbps) + L"k')\r\n";
                    script += L"  $p2 += $subtitleCodecArgs\r\n";
                    script += L"  $p2 += @($output)\r\n";
                }
                script += L"  Run-FF $p2\r\n";
                script += L"} finally { Remove-Item -LiteralPath ($passlog + '-0.log') -Force -ErrorAction SilentlyContinue; Remove-Item -LiteralPath ($passlog + '-0.log.mbtree') -Force -ErrorAction SilentlyContinue }\r\n";
            } else {
                if (audioNone) {
                    script += L"$a = $common + $overwriteArgs + @('-map','" + videoMap + L"','-an') + $subtitleMapArgs\r\n";
                } else {
                    script += L"$a = $common + $overwriteArgs + @('-map','" + videoMap + L"') + $audioMapArgs + $subtitleMapArgs\r\n";
                }
                if (!vf.empty()) script += L"$a += @('-vf'," + PowerShellQuote(vf) + L")\r\n";
                if (useManagedAudio && managedAudioHasOffset && !audioCopy && !audioNone) {
                    script += L"$a += $audioVideoFilterArgs + @('-shortest')\r\n";
                } else if (!afVideo.empty() && !audioCopy && !audioNone) {
                    script += L"$a += @('-af'," + PowerShellQuote(afVideo) + L",'-shortest')\r\n";
                }
                if (g_cutMode == CutterOutputMode::Mp4H264) {
                    script += L"$a += @('-c:v','libx264','-preset','medium','-b:v',(\"${videoK}k\"),'-pix_fmt','yuv420p')\r\n";
                    if (audioCopy) script += L"$a += @('-c:a','copy')\r\n";
                    else if (!audioNone) script += L"$a += @('-c:a','aac','-b:a','" + std::to_wstring(jobAudioKbps) + L"k')\r\n";
                    script += L"$a += $subtitleCodecArgs\r\n";
                    script += L"$a += @('-movflags','+faststart',$output)\r\n";
                } else {
                    script += L"$a += @('-c:v','libvpx-vp9','-deadline','good','-cpu-used','2','-b:v',(\"${videoK}k\"))\r\n";
                    if (audioCopy) script += L"$a += @('-c:a','copy')\r\n";
                    else if (!audioNone) script += L"$a += @('-c:a','libopus','-b:a','" + std::to_wstring(jobAudioKbps) + L"k')\r\n";
                    script += L"$a += $subtitleCodecArgs\r\n";
                    script += L"$a += @($output)\r\n";
                }
                script += L"Run-FF $a\r\n";
            }
        } else {
            if (audioNone) {
                script += L"$a = $common + $overwriteArgs + @('-map','" + videoMap + L"','-an') + $subtitleMapArgs\r\n";
            } else {
                script += L"$a = $common + $overwriteArgs + @('-map','" + videoMap + L"') + $audioMapArgs + $subtitleMapArgs\r\n";
            }
            if (!vf.empty()) script += L"$a += @('-vf'," + PowerShellQuote(vf) + L")\r\n";
            if (useManagedAudio && managedAudioHasOffset && !audioCopy && !audioNone) {
                script += L"$a += $audioVideoFilterArgs + @('-shortest')\r\n";
            } else if (!afVideo.empty() && !audioCopy && !audioNone) {
                script += L"$a += @('-af'," + PowerShellQuote(afVideo) + L",'-shortest')\r\n";
            }
            if (g_cutMode == CutterOutputMode::Mp4H264) {
                script += L"$a += @('-c:v','libx264','-preset','medium','-crf','" + std::to_wstring(jobCrf) + L"','-pix_fmt','yuv420p')\r\n";
                if (audioCopy) script += L"$a += @('-c:a','copy')\r\n";
                else if (!audioNone) script += L"$a += @('-c:a','aac','-b:a','" + std::to_wstring(jobAudioKbps) + L"k')\r\n";
                script += L"$a += $subtitleCodecArgs\r\n";
                script += L"$a += @('-movflags','+faststart',$output)\r\n";
            } else {
                script += L"$a += @('-c:v','libvpx-vp9','-crf','" + std::to_wstring(jobCrf) + L"','-b:v','0')\r\n";
                if (audioCopy) script += L"$a += @('-c:a','copy')\r\n";
                else if (!audioNone) script += L"$a += @('-c:a','libopus','-b:a','" + std::to_wstring(jobAudioKbps) + L"k')\r\n";
                script += L"$a += $subtitleCodecArgs\r\n";
                script += L"$a += @($output)\r\n";
            }
            script += L"Run-FF $a\r\n";
        }
    }

    script += L"Write-Host ''; Write-Host 'Done:' $output -ForegroundColor Green\r\n";
    script += L"Set-Content -LiteralPath $donePath -Value '0' -NoNewline\r\n";
    script += L"if (-not $closeWhenDone) { [void](Read-Host 'Press Enter to close') }\r\n";
    g_cutWorkerScriptPath = CutterTempPath(L".ps1");
    if (!WriteUtf8BomFile(g_cutWorkerScriptPath, script)) {
        MessageBoxW(g_main, L"Could not write the temporary PowerShell worker script.",
                    kAppTitle, MB_OK | MB_ICONERROR);

        CleanupCutterSyncedSubtitleTemps();
        return false;
    }

    const std::wstring command =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" +
        g_cutWorkerScriptPath + L"\"";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    // User-selectable console policy.  Visible is the default because the
    // console is a useful second source of truth for FFmpeg completion/errors.
    si.wShowWindow = showPowerShell ? SW_SHOWNORMAL : SW_HIDE;
    PROCESS_INFORMATION pi{};

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        MessageBoxW(g_main, L"Could not create the conversion job object.", kAppTitle, MB_OK | MB_ICONERROR);

        CleanupCutterSyncedSubtitleTemps();
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
    jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo));

    const DWORD createFlags = CREATE_SUSPENDED |
        (showPowerShell ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW);
    const BOOL ok = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                                   createFlags, nullptr, nullptr, &si, &pi);
    if (!ok) {
        CloseHandle(job);
        MessageBoxW(g_main, (L"Could not start PowerShell. Windows error " +
                    std::to_wstring(GetLastError())).c_str(), kAppTitle, MB_OK | MB_ICONERROR);

        CleanupCutterSyncedSubtitleTemps();
        return false;
    }
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(job);
        MessageBoxW(g_main, L"Could not attach the conversion worker to its kill-safe job.",
                    kAppTitle, MB_OK | MB_ICONERROR);

        CleanupCutterSyncedSubtitleTemps();
        return false;
    }

    g_cutWorkerJob = job;
    g_cutWorkerProcess = pi.hProcess;
    g_cutWorkerPid = pi.dwProcessId;
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    if (showPowerShell) {
        Sleep(80);
        SetForegroundWindow(g_main);
        BringWindowToTop(g_main);
    }

    SyncCutterControls();
    ShowOsdText(showPowerShell
                    ? (closePowerShellWhenDone ? L"Convert started (PowerShell visible)"
                                               : L"Convert started (PowerShell will stay open)")
                    : L"Convert started",
                1800);
    return true;
}

bool IsStatsOverlayAvailable() {
    if (!g_mpvReady || !g_mpvApi.free_node_contents) return false;

    mpv_node root{};
    if (g_mpvApi.get_property(
            g_mpv, "input-bindings", MPV_FORMAT_NODE, &root) < 0) {
        // Older/unusual libmpv builds may not expose input-bindings. In that
        // case, don't reject Media Info solely on the failed capability check.
        return true;
    }

    bool found = false;
    if (root.format == MPV_FORMAT_NODE_ARRAY && root.u.list) {
        for (int i = 0; i < root.u.list->num; ++i) {
            const mpv_node& item = root.u.list->values[i];
            if (item.format != MPV_FORMAT_NODE_MAP) continue;

            const std::string owner = NodeString(MapValue(item, "owner"));
            const std::string cmd = NodeString(MapValue(item, "cmd"));

            if (owner == "stats" ||
                cmd.find("script-binding stats/") != std::string::npos ||
                cmd.find("script-message-to stats ") != std::string::npos) {
                found = true;
                break;
            }
        }
    }

    g_mpvApi.free_node_contents(&root);
    return found;
}

std::vector<TrackInfo> GetTracks(const char* typeFilter = nullptr) {
    std::vector<TrackInfo> tracks;
    if (!g_mpvReady || !g_mpvApi.free_node_contents) return tracks;
    mpv_node root{};
    if (g_mpvApi.get_property(g_mpv, "track-list", MPV_FORMAT_NODE, &root) < 0) return tracks;
    if (root.format == MPV_FORMAT_NODE_ARRAY && root.u.list) {
        for (int i = 0; i < root.u.list->num; ++i) {
            const mpv_node& item = root.u.list->values[i];
            if (item.format != MPV_FORMAT_NODE_MAP) continue;
            TrackInfo t;
            t.type = NodeString(MapValue(item, "type"));
            if (typeFilter && t.type != typeFilter) continue;
            t.id = NodeInt(MapValue(item, "id"), -1);
            if (t.id < 0) continue;
            t.ffIndex = static_cast<int>(NodeInt(MapValue(item, "ff-index"), -1));
            t.lang = NodeString(MapValue(item, "lang"));
            t.title = NodeString(MapValue(item, "title"));
            t.codec = NodeString(MapValue(item, "codec"));
            t.externalFilename = NodeString(MapValue(item, "external-filename"));
            t.selected = NodeFlag(MapValue(item, "selected"), false);
            tracks.push_back(std::move(t));
        }
    }
    g_mpvApi.free_node_contents(&root);
    return tracks;
}


std::wstring CutterPrimaryInputPath() {
    if (!g_cutInputs.empty()) return AbsoluteLocalPath(g_cutInputs.front());
    if (!g_currentMediaPath.empty()) return AbsoluteLocalPath(g_currentMediaPath);
    return L"";
}

void ResetCutterManagedAudioTracksForSource() {
    g_cutManagedAudioSourcePath = CutterPrimaryInputPath();
    g_cutAudioTracksInitialized = false;
    g_cutAudioTracks.clear();
    g_cutAudioSyncTrackIndex = -1;
    if (g_cutAudioOffset) SetWindowTextW(g_cutAudioOffset, L"0");
    if (g_cutTracks) InvalidateRect(g_cutTracks, nullptr, FALSE);
    if (g_cutAudioSyncButton) InvalidateRect(g_cutAudioSyncButton, nullptr, FALSE);
}

void EnsureCutterManagedAudioSource() {
    const std::wstring source = CutterPrimaryInputPath();
    if (_wcsicmp(source.c_str(), g_cutManagedAudioSourcePath.c_str()) == 0) return;
    g_cutManagedAudioSourcePath = source;
    g_cutAudioTracksInitialized = false;
    g_cutAudioTracks.clear();
    g_cutAudioSyncTrackIndex = -1;
    if (g_cutAudioOffset) SetWindowTextW(g_cutAudioOffset, L"0");
    if (g_cutTracks) InvalidateRect(g_cutTracks, nullptr, FALSE);
    if (g_cutAudioSyncButton) InvalidateRect(g_cutAudioSyncButton, nullptr, FALSE);
}

std::wstring CutterManagedAudioDetails(const std::wstring& language,
                                       const std::wstring& title) {
    if (!language.empty() && !title.empty()) return language + L" | " + title;
    if (!title.empty()) return title;
    if (!language.empty()) return language;
    return L"";
}

void EnsureCutterAudioTracksInitialized() {
    EnsureCutterManagedAudioSource();
    if (g_cutAudioTracksInitialized) return;

    g_cutAudioTracks.clear();
    int embeddedIndex = 0;
    int selectedManagedIndex = -1;
    const auto tracks = GetTracks("audio");
    for (const auto& track : tracks) {
        // mpv can expose external tracks of its own. Cutter-managed external
        // audio is represented explicitly and gets a real FFmpeg input later.
        if (!track.externalFilename.empty()) continue;

        CutterManagedAudioTrack managed{};
        managed.embedded = true;
        managed.streamIndex = embeddedIndex++;
        managed.mpvTrackId = track.id;
        managed.language = Utf8ToWide(UpperAscii(track.lang));
        managed.title = Utf8ToWide(track.title);
        managed.included = false;
        managed.durationSeconds = g_duration > 0.0 ? g_duration : -1.0;
        if (track.selected && selectedManagedIndex < 0) {
            selectedManagedIndex = static_cast<int>(g_cutAudioTracks.size());
        }
        g_cutAudioTracks.push_back(std::move(managed));
    }

    // Cutter is primarily a clipper: default to the audio mpv is actually
    // playing, rather than carrying every dub/commentary stream into a clip.
    // If mpv exposes no selected embedded stream, fall back to the first one.
    if (!g_cutAudioTracks.empty()) {
        const size_t defaultIndex = selectedManagedIndex >= 0
            ? static_cast<size_t>(selectedManagedIndex)
            : 0u;
        g_cutAudioTracks[defaultIndex].included = true;
    }

    g_cutAudioTracksInitialized = true;

    // Preserve the old single global offset once when upgrading into the
    // per-track model.  After that, timing belongs to the selected audio row
    // and is intentionally source/session specific.
    if (g_cutAudioOffsetMs != 0 && !g_cutAudioTracks.empty()) {
        const size_t defaultIndex = selectedManagedIndex >= 0
            ? static_cast<size_t>(selectedManagedIndex)
            : 0u;
        g_cutAudioTracks[defaultIndex].offsetMs = g_cutAudioOffsetMs;
        g_cutAudioOffsetMs = 0;
        SaveSettings();
    }

    g_cutAudioSyncTrackIndex = -1;
    for (size_t i = 0; i < g_cutAudioTracks.size(); ++i) {
        if (g_cutAudioTracks[i].included) {
            g_cutAudioSyncTrackIndex = static_cast<int>(i);
            break;
        }
    }

    if (g_cutTracks) InvalidateRect(g_cutTracks, nullptr, FALSE);
    if (g_cutAudioSyncButton) InvalidateRect(g_cutAudioSyncButton, nullptr, FALSE);
}

CutterManagedAudioTrack* ActiveManagedAudioSyncTrack() {
    if (!g_cutAudioTracksInitialized ||
        g_cutAudioSyncTrackIndex < 0 ||
        g_cutAudioSyncTrackIndex >= static_cast<int>(g_cutAudioTracks.size())) {
        return nullptr;
    }
    auto& track = g_cutAudioTracks[static_cast<size_t>(g_cutAudioSyncTrackIndex)];
    return track.included ? &track : nullptr;
}

const CutterManagedAudioTrack* ActiveManagedAudioSyncTrackConst() {
    if (!g_cutAudioTracksInitialized ||
        g_cutAudioSyncTrackIndex < 0 ||
        g_cutAudioSyncTrackIndex >= static_cast<int>(g_cutAudioTracks.size())) {
        return nullptr;
    }
    const auto& track = g_cutAudioTracks[static_cast<size_t>(g_cutAudioSyncTrackIndex)];
    return track.included ? &track : nullptr;
}

bool CutterHasActiveAudioSyncTarget() {
    return ActiveManagedAudioSyncTrackConst() != nullptr;
}

std::wstring ManagedAudioSyncLabel(const CutterManagedAudioTrack& track) {
    std::wstring label;
    if (track.embedded) {
        label = L"Embedded audio " + std::to_wstring(track.streamIndex + 1);
    } else {
        label = std::filesystem::path(track.externalPath).filename().wstring();
        if (label.empty()) label = L"External audio";
    }

    const std::wstring details =
        CutterManagedAudioDetails(track.language, track.title);
    if (!details.empty()) label += L" | " + details;
    return label;
}

bool CutterCommitAudioSyncOffsetEdit(bool showError) {
    auto* track = ActiveManagedAudioSyncTrack();
    if (!track || !g_cutAudioOffset) return true;

    int parsed = track->offsetMs;
    if (!CutterParseAudioOffsetField(parsed, showError)) return false;
    track->offsetMs = parsed;
    return true;
}

void LoadManagedAudioSyncToControls() {
    const auto* track = ActiveManagedAudioSyncTrackConst();
    if (g_cutAudioOffset) {
        SetWindowTextW(
            g_cutAudioOffset,
            std::to_wstring(track ? track->offsetMs : 0).c_str());
    }
}

void CutterApplyAudioPreviewDelay() {
    if (!g_mpvReady) return;
    const auto* track = ActiveManagedAudioSyncTrackConst();
    const double delaySeconds =
        g_cutAudioPreviewBaseDelaySeconds +
        ((track && track->embedded)
             ? static_cast<double>(track->offsetMs) / 1000.0
             : 0.0);
    MpvSetDouble("audio-delay", delaySeconds);
}

void RefreshCutterAudioSyncTrackCombo() {
    if (!g_cutAudioSyncTrack) return;

    if (g_cutInputs.size() <= 1 &&
        (!g_cutInputs.empty() || !g_currentMediaPath.empty())) {
        EnsureCutterAudioTracksInitialized();
    }

    if (!CutterHasActiveAudioSyncTarget()) {
        g_cutAudioSyncTrackIndex = -1;
        for (size_t i = 0; i < g_cutAudioTracks.size(); ++i) {
            if (g_cutAudioTracks[i].included) {
                g_cutAudioSyncTrackIndex = static_cast<int>(i);
                break;
            }
        }
    }

    SendMessageW(g_cutAudioSyncTrack, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_cutAudioSyncTrack, CB_RESETCONTENT, 0, 0);

    int selectedCombo = -1;
    for (size_t i = 0; i < g_cutAudioTracks.size(); ++i) {
        const auto& track = g_cutAudioTracks[i];
        if (!track.included) continue;
        const std::wstring label = ManagedAudioSyncLabel(track);
        const int combo = static_cast<int>(SendMessageW(
            g_cutAudioSyncTrack,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(label.c_str())));
        if (combo >= 0) {
            SendMessageW(
                g_cutAudioSyncTrack,
                CB_SETITEMDATA,
                combo,
                static_cast<LPARAM>(i));
            if (static_cast<int>(i) == g_cutAudioSyncTrackIndex) {
                selectedCombo = combo;
            }
        }
    }

    if (selectedCombo < 0 &&
        SendMessageW(g_cutAudioSyncTrack, CB_GETCOUNT, 0, 0) > 0) {
        selectedCombo = 0;
        const LRESULT itemData = SendMessageW(
            g_cutAudioSyncTrack, CB_GETITEMDATA, 0, 0);
        if (itemData != CB_ERR) {
            g_cutAudioSyncTrackIndex = static_cast<int>(itemData);
        }
    }

    if (selectedCombo >= 0) {
        SendMessageW(g_cutAudioSyncTrack, CB_SETCURSEL, selectedCombo, 0);
    }

    SendMessageW(g_cutAudioSyncTrack, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_cutAudioSyncTrack, nullptr, TRUE);
}

bool CutterSelectManagedAudioSyncTrack(int trackIndex) {
    if (trackIndex < 0 ||
        trackIndex >= static_cast<int>(g_cutAudioTracks.size()) ||
        !g_cutAudioTracks[static_cast<size_t>(trackIndex)].included) {
        return false;
    }

    if (!CutterCommitAudioSyncOffsetEdit(false)) return false;

    g_cutAudioSyncTrackIndex = trackIndex;
    const auto& track = g_cutAudioTracks[static_cast<size_t>(trackIndex)];
    if (track.embedded && track.mpvTrackId >= 0 && g_mpvReady) {
        int64_t id = track.mpvTrackId;
        if (g_mpvApi.set_property(g_mpv, "aid", MPV_FORMAT_INT64, &id) >= 0) {
            g_pollCounter = 3;
            PollMpv();
        }
    }

    LoadManagedAudioSyncToControls();
    CutterApplyAudioPreviewDelay();
    SyncCutterAudioContextControls();
    return true;
}


bool CutterManagedSubtitleDriftReady(
    const CutterManagedSubtitleTrack& track) {
    if (!track.syncASet || !track.syncBSet) {
        return false;
    }

    return CutterSubtitleDriftGeometryValid(
        track.syncAVideoSeconds,
        track.syncAOffsetMs,
        track.syncBVideoSeconds,
        track.syncBOffsetMs);
}

std::wstring CutterManagedSubtitleDetails(const std::wstring& language,
                                          const std::wstring& title) {
    if (!language.empty() && !title.empty()) return language + L" | " + title;
    if (!title.empty()) return title;
    if (!language.empty()) return language;
    return L"";
}

void ResetCutterManagedSubtitleTracksForSource(bool preserveExternal) {
    std::vector<CutterManagedSubtitleTrack> externalTracks;
    if (preserveExternal) {
        externalTracks.reserve(g_cutSubtitleTracks.size());
        for (const auto& track : g_cutSubtitleTracks) {
            if (!track.embedded && !track.sourceBound) {
                externalTracks.push_back(track);
            }
        }
    }

    g_cutManagedSubtitleSourcePath = CutterPrimaryInputPath();
    g_cutSubtitleTracksInitialized = false;
    g_cutSubtitleTracks = std::move(externalTracks);
    g_cutSubtitleSyncTrackIndex = -1;
    g_cutSubtitleProbeError.clear();
    if (g_cutSubtitles) InvalidateRect(g_cutSubtitles, nullptr, FALSE);
}

void EnsureCutterManagedSubtitleSource() {
    const std::wstring source = CutterPrimaryInputPath();
    if (_wcsicmp(source.c_str(), g_cutManagedSubtitleSourcePath.c_str()) == 0) return;

    // Embedded rows belong to the source file; external rows belong to the
    // Cutter job. Keep the external rows when a new source is opened, then
    // rebuild the embedded portion lazily from mpv when SUBS OPT is opened.
    ResetCutterManagedSubtitleTracksForSource(true);
}

void EnsureCutterSubtitleTracksInitialized() {
    EnsureCutterManagedSubtitleSource();
    if (g_cutSubtitleTracksInitialized) return;

    std::vector<CutterManagedSubtitleTrack> externalTracks;
    externalTracks.reserve(g_cutSubtitleTracks.size() + 1);
    for (const auto& track : g_cutSubtitleTracks) {
        if (!track.embedded) externalTracks.push_back(track);
    }

    const std::wstring legacyExternalPath =
        g_cutSubtitlePath.empty() ? L"" : AbsoluteLocalPath(g_cutSubtitlePath);

    // mpv owns player preview identity; ffprobe owns exact FFmpeg stream identity.
    // Keep both. This avoids assuming mpv track id/order is the same thing as
    // FFmpeg's global stream index when an embedded subtitle is extracted.
    std::vector<TrackInfo> embeddedMpvTracks;
    for (const auto& track : GetTracks("sub")) {
        if (track.externalFilename.empty()) embeddedMpvTracks.push_back(track);
    }

    std::vector<winterstatic::subtitle::SubtitleTrack> probedTracks;
    g_cutSubtitleProbeError.clear();
    const std::wstring sourcePath = CutterPrimaryInputPath();
    const std::wstring ffprobePath = FindCutterTool(L"ffprobe.exe");
    if (!sourcePath.empty() && !embeddedMpvTracks.empty()) {
        if (ffprobePath.empty()) {
            g_cutSubtitleProbeError = L"ffprobe.exe was not found; embedded subtitle sync is unavailable.";
        } else {
            try {
                winterstatic::subtitle::ToolPaths tools;
                tools.ffprobe = std::filesystem::path(ffprobePath);
                probedTracks = winterstatic::subtitle::probe_subtitle_tracks(
                    std::filesystem::path(sourcePath), tools);
            } catch (const std::exception& e) {
                g_cutSubtitleProbeError =
                    L"Embedded subtitle probe failed: " + Utf8ToWide(e.what());
            }
        }
    }

    std::vector<bool> probeUsed(probedTracks.size(), false);
    auto metadataCompatible = [](
        const TrackInfo& mpvTrack,
        const winterstatic::subtitle::SubtitleTrack& probeTrack) {
        if (!mpvTrack.lang.empty() && !probeTrack.language.empty() &&
            UpperAscii(mpvTrack.lang) != UpperAscii(probeTrack.language)) {
            return false;
        }
        if (!mpvTrack.title.empty() && !probeTrack.title.empty() &&
            mpvTrack.title != probeTrack.title) {
            return false;
        }
        if (!mpvTrack.codec.empty() && !probeTrack.codec.empty() &&
            UpperAscii(mpvTrack.codec) != UpperAscii(probeTrack.codec)) {
            return false;
        }
        return true;
    };

    g_cutSubtitleTracks.clear();
    int embeddedIndex = 0;
    for (size_t mpvIndex = 0; mpvIndex < embeddedMpvTracks.size(); ++mpvIndex) {
        const auto& track = embeddedMpvTracks[mpvIndex];

        CutterManagedSubtitleTrack managed{};
        managed.embedded = true;
        managed.streamIndex = embeddedIndex++;
        managed.mpvTrackId = track.id;
        managed.language = Utf8ToWide(UpperAscii(track.lang));
        managed.title = Utf8ToWide(track.title);
        managed.codec = Utf8ToWide(track.codec);

        int probeMatch = -1;

        // mpv's ff-index is the FFmpeg global stream index. When mpv exposes
        // it, that identity is authoritative for the same loaded file; do not
        // reject an exact index match merely because mpv/ffprobe render title,
        // language or codec metadata slightly differently.
        if (track.ffIndex >= 0) {
            for (size_t i = 0; i < probedTracks.size(); ++i) {
                if (!probeUsed[i] &&
                    probedTracks[i].stream_index == track.ffIndex) {
                    probeMatch = static_cast<int>(i);
                    break;
                }
            }
        }

        // If ff-index is unavailable, first try an unambiguous metadata match.
        if (probeMatch < 0) {
            int onlyMatch = -1;
            int matchCount = 0;
            for (size_t i = 0; i < probedTracks.size(); ++i) {
                if (probeUsed[i] || !metadataCompatible(track, probedTracks[i])) continue;
                // Require at least one useful metadata field for this path;
                // otherwise several anonymous subtitle tracks would all match.
                const bool hasSignal =
                    !track.lang.empty() || !track.title.empty() || !track.codec.empty();
                if (!hasSignal) continue;
                onlyMatch = static_cast<int>(i);
                ++matchCount;
            }
            if (matchCount == 1) probeMatch = onlyMatch;
        }

        // Final fallback: both lists describe embedded subtitle streams from the
        // same primary file. If their counts agree, pair the remaining rows by
        // subtitle-stream order. This remains deliberately lower priority than
        // exact ff-index and unambiguous metadata matching so cosmetic metadata
        // differences cannot leave otherwise valid tracks unmatched.
        if (probeMatch < 0 &&
            embeddedMpvTracks.size() == probedTracks.size() &&
            mpvIndex < probedTracks.size() &&
            !probeUsed[mpvIndex]) {
            probeMatch = static_cast<int>(mpvIndex);
        }

        if (probeMatch >= 0) {
            const auto& probed = probedTracks[static_cast<size_t>(probeMatch)];
            probeUsed[static_cast<size_t>(probeMatch)] = true;
            managed.ffmpegStreamIndex = probed.stream_index;
            managed.codec = Utf8ToWide(probed.codec);
            managed.syncCapable =
                !winterstatic::subtitle::is_known_bitmap_codec(probed.codec);
        }

        // Cutter is primarily a clipper: embedded subtitle tracks start OUT.
        // The user can opt individual tracks back in from SUBS OPT.
        managed.included = false;
        managed.mode = CutterSubtitleMode::Soft;
        g_cutSubtitleTracks.push_back(std::move(managed));
    }

    if (g_cutSubtitleProbeError.empty() && !embeddedMpvTracks.empty()) {
        const bool anyEmbeddedSyncCapable = std::any_of(
            g_cutSubtitleTracks.begin(),
            g_cutSubtitleTracks.end(),
            [](const CutterManagedSubtitleTrack& candidate) {
                return candidate.embedded && candidate.syncCapable &&
                       candidate.ffmpegStreamIndex >= 0 && candidate.mpvTrackId >= 0;
            });
        if (!anyEmbeddedSyncCapable) {
            if (probedTracks.empty()) {
                g_cutSubtitleProbeError =
                    L"ffprobe did not return any embedded subtitle streams for this input.";
            } else {
                g_cutSubtitleProbeError =
                    L"Embedded subtitle streams were found, but WinterStatic could not match them to the player tracks.";
            }
        }
    }

    for (auto& external : externalTracks) {
        g_cutSubtitleTracks.push_back(std::move(external));
    }

    if (!legacyExternalPath.empty()) {
        int existingIndex = -1;
        for (size_t i = 0; i < g_cutSubtitleTracks.size(); ++i) {
            const auto& track = g_cutSubtitleTracks[i];
            if (track.embedded) continue;
            if (_wcsicmp(
                    AbsoluteLocalPath(track.externalPath).c_str(),
                    legacyExternalPath.c_str()) == 0) {
                existingIndex = static_cast<int>(i);
                break;
            }
        }

        if (existingIndex < 0) {
            CutterManagedSubtitleTrack external{};
            external.embedded = false;
            external.externalPath = legacyExternalPath;
            const std::wstring pairedForSource =
                CutterFindMatchingSubtitle(CutterPrimaryInputPath());
            external.sourceBound =
                !pairedForSource.empty() &&
                _wcsicmp(
                    AbsoluteLocalPath(pairedForSource).c_str(),
                    legacyExternalPath.c_str()) == 0;
            external.title = g_cutSubtitleTitle;
            external.included = g_cutSubtitlesEnabled;
            external.mode = g_cutSubtitleMode;
            external.offsetMs = g_cutSubtitleOffsetMs;
            external.syncASet = g_cutSubtitleSyncASet;
            external.syncBSet = g_cutSubtitleSyncBSet;
            external.syncAVideoSeconds = g_cutSubtitleSyncAVideoSeconds;
            external.syncBVideoSeconds = g_cutSubtitleSyncBVideoSeconds;
            external.syncAOffsetMs = g_cutSubtitleSyncAOffsetMs;
            external.syncBOffsetMs = g_cutSubtitleSyncBOffsetMs;
            external.driftEnabled = g_cutSubtitleDriftEnabled;
            existingIndex = static_cast<int>(g_cutSubtitleTracks.size());
            g_cutSubtitleTracks.push_back(std::move(external));
        }
        g_cutSubtitleSyncTrackIndex = existingIndex;
    } else {
        g_cutSubtitleSyncTrackIndex = -1;
        for (size_t i = 0; i < g_cutSubtitleTracks.size(); ++i) {
            const auto& candidate = g_cutSubtitleTracks[i];
            const bool syncCapable = candidate.embedded
                ? (candidate.syncCapable && candidate.ffmpegStreamIndex >= 0 && candidate.mpvTrackId >= 0)
                : !candidate.externalPath.empty();
            if (syncCapable) {
                g_cutSubtitleSyncTrackIndex = static_cast<int>(i);
                break;
            }
        }
    }

    g_cutSubtitleTracksInitialized = true;
}

bool CutterManagedSubtitleSyncCapable(
    const CutterManagedSubtitleTrack& track) {
    if (!track.embedded) return !track.externalPath.empty();
    return track.syncCapable && track.ffmpegStreamIndex >= 0 && track.mpvTrackId >= 0;
}

CutterManagedSubtitleTrack* ActiveManagedSubtitleSyncTrack() {
    if (!g_cutSubtitleTracksInitialized) return nullptr;
    if (g_cutSubtitleSyncTrackIndex < 0 ||
        g_cutSubtitleSyncTrackIndex >= static_cast<int>(g_cutSubtitleTracks.size())) {
        return nullptr;
    }
    auto& track =
        g_cutSubtitleTracks[static_cast<size_t>(g_cutSubtitleSyncTrackIndex)];
    if (!CutterManagedSubtitleSyncCapable(track)) return nullptr;
    return &track;
}

const CutterManagedSubtitleTrack* ActiveManagedSubtitleSyncTrackConst() {
    if (!g_cutSubtitleTracksInitialized) return nullptr;
    if (g_cutSubtitleSyncTrackIndex < 0 ||
        g_cutSubtitleSyncTrackIndex >= static_cast<int>(g_cutSubtitleTracks.size())) {
        return nullptr;
    }
    const auto& track =
        g_cutSubtitleTracks[static_cast<size_t>(g_cutSubtitleSyncTrackIndex)];
    if (!CutterManagedSubtitleSyncCapable(track)) return nullptr;
    return &track;
}

void StoreLegacySubtitleSyncToManaged() {
    auto* track = ActiveManagedSubtitleSyncTrack();
    if (!track) return;
    track->offsetMs = g_cutSubtitleOffsetMs;
    track->syncASet = g_cutSubtitleSyncASet;
    track->syncBSet = g_cutSubtitleSyncBSet;
    track->syncAVideoSeconds = g_cutSubtitleSyncAVideoSeconds;
    track->syncBVideoSeconds = g_cutSubtitleSyncBVideoSeconds;
    track->syncAOffsetMs = g_cutSubtitleSyncAOffsetMs;
    track->syncBOffsetMs = g_cutSubtitleSyncBOffsetMs;
    track->driftEnabled = g_cutSubtitleDriftEnabled;
}

void LoadManagedSubtitleSyncToLegacy() {
    const auto* track = ActiveManagedSubtitleSyncTrackConst();
    if (!track) {
        g_cutSubtitleOffsetMs = 0;
        g_cutSubtitleSyncASet = false;
        g_cutSubtitleSyncBSet = false;
        g_cutSubtitleSyncAVideoSeconds = 0.0;
        g_cutSubtitleSyncBVideoSeconds = 0.0;
        g_cutSubtitleSyncAOffsetMs = 0;
        g_cutSubtitleSyncBOffsetMs = 0;
        g_cutSubtitleDriftEnabled = false;
        g_cutSubtitlesEnabled = false;
        g_cutSubtitlePath.clear();
        g_cutSubtitleTitle.clear();
        return;
    }

    g_cutSubtitleOffsetMs = track->offsetMs;
    g_cutSubtitleSyncASet = track->syncASet;
    g_cutSubtitleSyncBSet = track->syncBSet;
    g_cutSubtitleSyncAVideoSeconds = track->syncAVideoSeconds;
    g_cutSubtitleSyncBVideoSeconds = track->syncBVideoSeconds;
    g_cutSubtitleSyncAOffsetMs = track->syncAOffsetMs;
    g_cutSubtitleSyncBOffsetMs = track->syncBOffsetMs;
    g_cutSubtitleDriftEnabled = track->driftEnabled;

    // Keep the old persisted single-external fields aligned only when an
    // external managed row is active. Embedded sync is session/job state and
    // must not erase or repurpose the user's persisted external subtitle path.
    if (!track->embedded) {
        g_cutSubtitlePath = track->externalPath;
        g_cutSubtitlesEnabled = track->included;
        g_cutSubtitleMode = track->mode;
        g_cutSubtitleTitle = track->title;
    }
}

std::wstring ManagedSubtitleSyncLabel(const CutterManagedSubtitleTrack& track) {
    if (track.embedded) {
        return L"Embedded " + std::to_wstring(track.streamIndex + 1);
    }
    std::wstring name =
        std::filesystem::path(track.externalPath).filename().wstring();
    if (name.empty()) name = track.externalPath;
    return name;
}

void RefreshCutterSubtitleSyncTrackCombo() {
    if (!g_cutSubSyncTrack) return;

    if (g_cutSubtitleTracksInitialized) {
        const auto activeValid = [&]() {
            if (g_cutSubtitleSyncTrackIndex < 0 ||
                g_cutSubtitleSyncTrackIndex >= static_cast<int>(g_cutSubtitleTracks.size())) {
                return false;
            }
            const auto& track =
                g_cutSubtitleTracks[static_cast<size_t>(g_cutSubtitleSyncTrackIndex)];
            return track.included && CutterManagedSubtitleSyncCapable(track);
        };

        if (!activeValid()) {
            StoreLegacySubtitleSyncToManaged();
            g_cutSubtitleSyncTrackIndex = -1;
            for (size_t i = 0; i < g_cutSubtitleTracks.size(); ++i) {
                const auto& track = g_cutSubtitleTracks[i];
                if (track.included && CutterManagedSubtitleSyncCapable(track)) {
                    g_cutSubtitleSyncTrackIndex = static_cast<int>(i);
                    break;
                }
            }
            LoadManagedSubtitleSyncToLegacy();
        }
    }

    SendMessageW(g_cutSubSyncTrack, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_cutSubSyncTrack, CB_RESETCONTENT, 0, 0);

    int selectedComboIndex = -1;
    int comboIndex = 0;

    if (g_cutSubtitleTracksInitialized) {
        for (size_t i = 0; i < g_cutSubtitleTracks.size(); ++i) {
            const auto& track = g_cutSubtitleTracks[i];
            if (!track.included || !CutterManagedSubtitleSyncCapable(track)) continue;

            std::wstring label = ManagedSubtitleSyncLabel(track);
            const std::wstring details =
                CutterManagedSubtitleDetails(track.language, track.title);
            if (!details.empty()) label += L" | " + details;

            const LRESULT item = SendMessageW(
                g_cutSubSyncTrack,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(label.c_str()));
            if (item >= 0) {
                SendMessageW(
                    g_cutSubSyncTrack,
                    CB_SETITEMDATA,
                    static_cast<WPARAM>(item),
                    static_cast<LPARAM>(i));
                if (static_cast<int>(i) == g_cutSubtitleSyncTrackIndex) {
                    selectedComboIndex = comboIndex;
                }
                ++comboIndex;
            }
        }
    } else if (!g_cutSubtitlePath.empty()) {
        const std::wstring label =
            std::filesystem::path(g_cutSubtitlePath).filename().wstring();
        const std::wstring display = label.empty() ? g_cutSubtitlePath : label;
        const LRESULT item = SendMessageW(
            g_cutSubSyncTrack,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(display.c_str()));
        if (item >= 0) {
            SendMessageW(g_cutSubSyncTrack, CB_SETITEMDATA,
                         static_cast<WPARAM>(item), static_cast<LPARAM>(-1));
            selectedComboIndex = 0;
            comboIndex = 1;
        }
    }

    if (selectedComboIndex < 0 && comboIndex > 0) selectedComboIndex = 0;
    SendMessageW(
        g_cutSubSyncTrack,
        CB_SETCURSEL,
        selectedComboIndex >= 0 ? selectedComboIndex : static_cast<WPARAM>(-1),
        0);
    EnableWindow(g_cutSubSyncTrack, comboIndex > 0 ? TRUE : FALSE);

    SendMessageW(g_cutSubSyncTrack, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_cutSubSyncTrack, nullptr, TRUE);
}

bool CutterSelectManagedSubtitleSyncTrack(int trackIndex) {
    if (!g_cutSubtitleTracksInitialized ||
        trackIndex < 0 ||
        trackIndex >= static_cast<int>(g_cutSubtitleTracks.size())) {
        return false;
    }

    const auto& candidate =
        g_cutSubtitleTracks[static_cast<size_t>(trackIndex)];
    if (!CutterManagedSubtitleSyncCapable(candidate)) return false;

    StoreLegacySubtitleSyncToManaged();
    g_cutSubtitleSyncTrackIndex = trackIndex;
    LoadManagedSubtitleSyncToLegacy();
    SyncCutterSubtitleContextControls();

    if (CutterHasActiveSubtitleSyncTarget()) {
        CutterSelectActiveSubtitleForPreview();
        CutterApplySubtitlePreviewDelay();
    }
    SaveSettings();
    return true;
}

bool CutterHasActiveSubtitleSyncTarget() {
    if (g_cutSubtitleTracksInitialized) {
        return ActiveManagedSubtitleSyncTrackConst() != nullptr;
    }
    return g_cutSubtitlesEnabled && !g_cutSubtitlePath.empty();
}

bool CutterHasIncludedSubtitleTracks() {
    if (g_cutSubtitleTracksInitialized) {
        return std::any_of(
            g_cutSubtitleTracks.begin(),
            g_cutSubtitleTracks.end(),
            [](const CutterManagedSubtitleTrack& track) {
                return track.included;
            });
    }
    return g_cutSubtitlesEnabled || g_cutKeepEmbeddedSubtitles;
}

void CutterSelectSubtitleForPreview(const std::wstring& rawPath) {
    if (!g_mpvReady || rawPath.empty()) return;

    const std::wstring wanted =
        AbsoluteLocalPath(rawPath);
    const auto tracks = GetTracks("sub");

    for (const auto& track : tracks) {
        if (track.externalFilename.empty()) continue;
        const std::wstring external =
            AbsoluteLocalPath(
                Utf8ToWide(track.externalFilename));
        if (_wcsicmp(
                external.c_str(),
                wanted.c_str()) == 0) {
            MpvCommand({
                "set",
                "sid",
                std::to_string(track.id)
            });
            CutterApplySubtitlePreviewDelay();
            return;
        }
    }

    if (MpvCommand({
            "sub-add",
            WideToUtf8(wanted),
            "select"
        })) {
        CutterApplySubtitlePreviewDelay();
    }
}

void CutterSelectActiveSubtitleForPreview() {
    if (!g_mpvReady) return;

    if (g_cutSubtitleTracksInitialized) {
        const auto* track = ActiveManagedSubtitleSyncTrackConst();
        if (!track) return;

        if (track->embedded) {
            if (track->mpvTrackId >= 0) {
                MpvCommand({
                    "set",
                    "sid",
                    std::to_string(track->mpvTrackId)
                });
                CutterApplySubtitlePreviewDelay();
            }
            return;
        }

        CutterSelectSubtitleForPreview(track->externalPath);
        return;
    }

    CutterSelectSubtitleForPreview(g_cutSubtitlePath);
}

std::vector<PlaylistEntry> GetPlaylistEntries() {
    std::vector<PlaylistEntry> entries;
    if (!g_mpvReady || !g_mpvApi.free_node_contents) return entries;

    mpv_node root{};
    if (g_mpvApi.get_property(g_mpv, "playlist", MPV_FORMAT_NODE, &root) < 0) {
        return entries;
    }

    if (root.format == MPV_FORMAT_NODE_ARRAY && root.u.list) {
        entries.reserve(static_cast<size_t>(root.u.list->num));
        for (int i = 0; i < root.u.list->num; ++i) {
            const mpv_node& item = root.u.list->values[i];
            if (item.format != MPV_FORMAT_NODE_MAP) continue;

            PlaylistEntry entry;
            entry.filename = NodeString(MapValue(item, "filename"));
            entry.title = NodeString(MapValue(item, "title"));
            entry.current = NodeFlag(MapValue(item, "current"), false);
            entry.playing = NodeFlag(MapValue(item, "playing"), false);
            entries.push_back(std::move(entry));
        }
    }

    g_mpvApi.free_node_contents(&root);
    return entries;
}

std::wstring PlaylistEntryLabel(const PlaylistEntry& entry, size_t index) {
    std::wstring label;
    if (!entry.title.empty()) {
        label = Utf8ToWide(entry.title);
    } else {
        const std::wstring full = Utf8ToWide(entry.filename);
        if (!full.empty()) {
            const std::filesystem::path path(full);
            label = path.filename().wstring();
            if (label.empty()) label = full;
        }
    }
    if (label.empty()) label = L"Untitled";
    if (label.size() > 100) {
        label.resize(97);
        label += L"...";
    }

    // Win32 menus treat '&' as a mnemonic marker. Escape literal ampersands.
    size_t pos = 0;
    while ((pos = label.find(L'&', pos)) != std::wstring::npos) {
        label.insert(pos, 1, L'&');
        pos += 2;
    }

    return std::to_wstring(index + 1) + L". " + label;
}

std::vector<double> GetChapterTimes() {
    std::vector<double> chapters;
    if (!g_mpvReady || !g_mpvApi.free_node_contents) return chapters;

    mpv_node root{};
    if (g_mpvApi.get_property(g_mpv, "chapter-list", MPV_FORMAT_NODE, &root) < 0) {
        return chapters;
    }

    if (root.format == MPV_FORMAT_NODE_ARRAY && root.u.list) {
        chapters.reserve(static_cast<size_t>(root.u.list->num));
        for (int i = 0; i < root.u.list->num; ++i) {
            const mpv_node& item = root.u.list->values[i];
            if (item.format != MPV_FORMAT_NODE_MAP) continue;

            const double time = NodeDouble(MapValue(item, "time"), -1.0);
            if (time >= 0.0) chapters.push_back(time);
        }
    }

    g_mpvApi.free_node_contents(&root);
    std::sort(chapters.begin(), chapters.end());
    return chapters;
}

void RefreshChapterMarkers() {
    const std::vector<double> chapters = GetChapterTimes();

    bool changed = chapters.size() != g_chapterTimes.size();
    if (!changed) {
        for (size_t i = 0; i < chapters.size(); ++i) {
            if (std::abs(chapters[i] - g_chapterTimes[i]) > 0.001) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        g_chapterTimes = chapters;
        if (g_seek) InvalidateRect(g_seek, nullptr, FALSE);
    }
}

std::wstring TrackShortLabel(const TrackInfo& t) {
    if (!t.lang.empty()) return Utf8ToWide(UpperAscii(t.lang));
    if (!t.title.empty()) return Utf8ToWide(t.title);
    wchar_t buf[32]{};
    swprintf_s(buf, L"#%lld", static_cast<long long>(t.id));
    return buf;
}

std::wstring TrackMenuLabel(const TrackInfo& t) {
    std::wstring lang = t.lang.empty() ? L"" : Utf8ToWide(UpperAscii(t.lang));
    std::wstring title = t.title.empty() ? L"" : Utf8ToWide(t.title);
    wchar_t idbuf[32]{};
    swprintf_s(idbuf, L"Track %lld", static_cast<long long>(t.id));
    if (!lang.empty() && !title.empty()) return lang + L" - " + title;
    if (!title.empty()) return title;
    if (!lang.empty()) return lang;
    return idbuf;
}

bool AsciiEqualInsensitive(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned char ac = static_cast<unsigned char>(a[i]);
        unsigned char bc = static_cast<unsigned char>(b[i]);
        if (ac >= 'A' && ac <= 'Z') ac = static_cast<unsigned char>(ac - 'A' + 'a');
        if (bc >= 'A' && bc <= 'Z') bc = static_cast<unsigned char>(bc - 'A' + 'a');
        if (ac != bc) return false;
    }
    return true;
}

void SaveRememberedTrackSettings() {
    WriteSettingInt(L"Tracks", L"LastAudioValid", g_haveRememberedAudio ? 1 : 0);
    WriteSettingString(L"Tracks", L"LastAudioLanguage", g_rememberedAudioLanguage);
    WriteSettingString(L"Tracks", L"LastAudioTitle", g_rememberedAudioTitle);

    WriteSettingInt(L"Tracks", L"LastSubtitleValid",
                    g_haveRememberedSubtitle ? 1 : 0);
    WriteSettingInt(L"Tracks", L"LastSubtitleOff",
                    g_rememberedSubtitleOff ? 1 : 0);
    WriteSettingString(L"Tracks", L"LastSubtitleLanguage",
                       g_rememberedSubtitleLanguage);
    WriteSettingString(L"Tracks", L"LastSubtitleTitle",
                       g_rememberedSubtitleTitle);
}

void RememberAudioTrack(const TrackInfo& track) {
    g_haveRememberedAudio = true;
    g_rememberedAudioLanguage = Utf8ToWide(track.lang);
    g_rememberedAudioTitle = Utf8ToWide(track.title);
    SaveRememberedTrackSettings();
}

void RememberSubtitleTrack(const TrackInfo& track) {
    g_haveRememberedSubtitle = true;
    g_rememberedSubtitleOff = false;
    g_rememberedSubtitleLanguage = Utf8ToWide(track.lang);
    g_rememberedSubtitleTitle = Utf8ToWide(track.title);
    SaveRememberedTrackSettings();
}

void RememberSubtitlesOff() {
    g_haveRememberedSubtitle = true;
    g_rememberedSubtitleOff = true;
    g_rememberedSubtitleLanguage.clear();
    g_rememberedSubtitleTitle.clear();
    SaveRememberedTrackSettings();
}

int FindRememberedTrackIndex(const std::vector<TrackInfo>& tracks,
                             const std::wstring& rememberedLanguage,
                             const std::wstring& rememberedTitle) {
    const std::string language = WideToUtf8(rememberedLanguage);
    const std::string title = WideToUtf8(rememberedTitle);

    // Strongest match: title plus language (when both were available).
    if (!title.empty()) {
        for (size_t i = 0; i < tracks.size(); ++i) {
            const bool titleMatches = tracks[i].title == title;
            const bool languageMatches =
                language.empty() || AsciiEqualInsensitive(tracks[i].lang, language);
            if (titleMatches && languageMatches) {
                return static_cast<int>(i);
            }
        }

        // Some files omit or change the language tag while keeping the same
        // descriptive title, so title-only is the next safest fallback.
        for (size_t i = 0; i < tracks.size(); ++i) {
            if (tracks[i].title == title) {
                return static_cast<int>(i);
            }
        }
    }

    // Finally fall back to the remembered language. If even that does not
    // exist, leave mpv's preferred-language/default choice untouched.
    if (!language.empty()) {
        for (size_t i = 0; i < tracks.size(); ++i) {
            if (AsciiEqualInsensitive(tracks[i].lang, language)) {
                return static_cast<int>(i);
            }
        }
    }

    return -1;
}

void ApplyRememberedTrackSelections() {
    if (!g_mpvReady) return;

    if (g_haveRememberedAudio) {
        const auto audioTracks = GetTracks("audio");
        const int index = FindRememberedTrackIndex(
            audioTracks, g_rememberedAudioLanguage, g_rememberedAudioTitle);

        if (index >= 0) {
            int64_t id = audioTracks[static_cast<size_t>(index)].id;
            g_mpvApi.set_property(g_mpv, "aid", MPV_FORMAT_INT64, &id);
        }
    }

    if (g_haveRememberedSubtitle) {
        if (g_rememberedSubtitleOff) {
            g_mpvApi.set_property_string(g_mpv, "sid", "no");
        } else {
            const auto subtitleTracks = GetTracks("sub");
            const int index = FindRememberedTrackIndex(
                subtitleTracks,
                g_rememberedSubtitleLanguage,
                g_rememberedSubtitleTitle);

            if (index >= 0) {
                int64_t id = subtitleTracks[static_cast<size_t>(index)].id;
                g_mpvApi.set_property(g_mpv, "sid", MPV_FORMAT_INT64, &id);
            }
        }
    }
}

void LayoutChildren(HWND hwnd);
void ShowPlaylistOsd();
void TogglePlaylistOsd();
void FlashPlaylistOsd(int durationMs = 2500);
void ShowPlaylistPopup(HWND anchor);
void SetMediaWindowTitle(const std::wstring& path);
std::wstring CurrentMediaTitleForOsd();
std::wstring AbsoluteLocalPath(const std::wstring& path);
std::wstring FindNextMediaFileInFolder(const std::wstring& currentPath);
void ExitFullscreen();

void UpdateTimeLabel() {
    if (!g_time) return;
    const std::wstring text = FormatTime(g_timePos) + L" / " + FormatTime(g_duration);
    SetWindowTextW(g_time, text.c_str());
}

void UpdateVolumeLabel() {
    if (!g_volumeText) return;
    wchar_t text[16]{};
    swprintf_s(text, L"%d%%", g_volumePercent);
    SetWindowTextW(g_volumeText, text);
}

void UpdateStatusText() {
    std::wstring status;
    std::wstring audioText = L"--";
    std::wstring subText = L"--";

    if (!g_mpvReady) {
        status = L"libmpv not loaded";
    } else {
        const bool idle = MpvGetFlag("idle-active", true);
        const bool cachePause = MpvGetFlag("paused-for-cache", false);
        const bool paused = MpvGetFlag("pause", true);

        if (idle) status = L"Stopped";
        else if (cachePause) status = L"Buffering";
        else if (paused) status = L"Paused";
        else status = L"Playing";

        mpv_node vp{};
        if (g_mpvApi.get_property(g_mpv, "video-params", MPV_FORMAT_NODE, &vp) >= 0) {
            if (vp.format == MPV_FORMAT_NODE_MAP) {
                const int64_t w = NodeInt(MapValue(vp, "w"), 0);
                const int64_t h = NodeInt(MapValue(vp, "h"), 0);
                if (w > 0 && h > 0) {
                    wchar_t res[64]{};
                    swprintf_s(res, L"  %lldx%lld",
                               static_cast<long long>(w), static_cast<long long>(h));
                    status += res;
                }
            }
            g_mpvApi.free_node_contents(&vp);
        }

        const auto allTracks = GetTracks(nullptr);
        bool haveSubTrack = false;

        for (const auto& t : allTracks) {
            if (t.type == "audio" && t.selected) {
                audioText = TrackShortLabel(t);
            } else if (t.type == "sub") {
                haveSubTrack = true;
                if (t.selected) {
                    subText = TrackShortLabel(t);
                }
            }
        }

        if (haveSubTrack && subText == L"--") {
            subText = L"OFF";
        }
    }

    const bool layoutChanged =
        status != g_statusText || audioText != g_audioInfoText || subText != g_subInfoText;

    g_statusText = status;
    g_audioInfoText = audioText;
    g_subInfoText = subText;

    if (g_status) SetWindowTextW(g_status, g_statusText.c_str());
    if (g_audioInfo) SetWindowTextW(g_audioInfo, g_audioInfoText.c_str());
    if (g_subInfo) SetWindowTextW(g_subInfo, g_subInfoText.c_str());

    // Track metadata often arrives just after the first layout. Re-layout as soon
    // as the real strings are known so they don't remain squeezed until a resize.
    if (layoutChanged && g_main) LayoutChildren(g_main);
}

// Taskbar glyphs reuse the same simple geometry language as the native
// toolbar controls. Their implementations live later with the other GDI helpers.
void DrawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width);
void DrawTriangle(HDC dc, POINT a, POINT b, POINT c, COLORREF color);

enum class TaskbarGlyph {
    Previous,
    Stop,
    Play,
    Pause,
    Next,
    Fullscreen
};

HICON CreateTaskbarGlyphIcon(TaskbarGlyph glyph) {
    const int width = std::max(16, GetSystemMetrics(SM_CXSMICON));
    const int height = std::max(16, GetSystemMetrics(SM_CYSMICON));

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* rawBits = nullptr;
    HBITMAP colorBitmap = CreateDIBSection(
        nullptr, &bmi, DIB_RGB_COLORS, &rawBits, nullptr, 0);
    if (!colorBitmap || !rawBits) {
        if (colorBitmap) DeleteObject(colorBitmap);
        return nullptr;
    }

    std::memset(rawBits, 0,
                static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) {
        DeleteObject(colorBitmap);
        return nullptr;
    }
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(dc, colorBitmap));

    const int cx = width / 2;
    const int cy = height / 2;
    const int scale = std::max(1, std::min(width, height) / 16);
    const int halfH = std::max(5, 6 * scale);
    const int lineWidth = std::max(1, 2 * scale);
    const COLORREF fg = C_TEXT;

    switch (glyph) {
    case TaskbarGlyph::Play:
        DrawTriangle(dc,
                     { cx - 4 * scale, cy - halfH },
                     { cx - 4 * scale, cy + halfH },
                     { cx + 6 * scale, cy }, fg);
        break;
    case TaskbarGlyph::Pause: {
        const int barW = std::max(2, 3 * scale);
        RECT left{ cx - 5 * scale, cy - halfH,
                   cx - 5 * scale + barW, cy + halfH + 1 };
        RECT right{ cx + 2 * scale, cy - halfH,
                    cx + 2 * scale + barW, cy + halfH + 1 };
        FillSolid(dc, left, fg);
        FillSolid(dc, right, fg);
        break;
    }
    case TaskbarGlyph::Stop: {
        const int half = std::max(4, 5 * scale);
        RECT stop{ cx - half, cy - half, cx + half + 1, cy + half + 1 };
        FillSolid(dc, stop, fg);
        break;
    }
    case TaskbarGlyph::Previous:
        DrawTriangle(dc,
                     { cx + 6 * scale, cy - halfH },
                     { cx + 6 * scale, cy + halfH },
                     { cx, cy }, fg);
        DrawTriangle(dc,
                     { cx, cy - halfH },
                     { cx, cy + halfH },
                     { cx - 6 * scale, cy }, fg);
        break;
    case TaskbarGlyph::Next:
        DrawTriangle(dc,
                     { cx - 6 * scale, cy - halfH },
                     { cx - 6 * scale, cy + halfH },
                     { cx, cy }, fg);
        DrawTriangle(dc,
                     { cx, cy - halfH },
                     { cx, cy + halfH },
                     { cx + 6 * scale, cy }, fg);
        break;
    case TaskbarGlyph::Fullscreen: {
        const int inset = 2 * scale;
        const int arm = std::max(4, 5 * scale);
        const int left = inset;
        const int top = inset;
        const int right = width - 1 - inset;
        const int bottom = height - 1 - inset;
        DrawLine(dc, left, top, left + arm, top, fg, lineWidth);
        DrawLine(dc, left, top, left, top + arm, fg, lineWidth);
        DrawLine(dc, right - arm, top, right, top, fg, lineWidth);
        DrawLine(dc, right, top, right, top + arm, fg, lineWidth);
        DrawLine(dc, left, bottom - arm, left, bottom, fg, lineWidth);
        DrawLine(dc, left, bottom, left + arm, bottom, fg, lineWidth);
        DrawLine(dc, right, bottom - arm, right, bottom, fg, lineWidth);
        DrawLine(dc, right - arm, bottom, right, bottom, fg, lineWidth);
        break;
    }
    }

    SelectObject(dc, oldBitmap);
    GdiFlush();
    DeleteDC(dc);

    // GDI drawing fills RGB but not the alpha byte. Promote every drawn pixel
    // to fully opaque while leaving the untouched background transparent.
    auto* pixels = static_cast<DWORD*>(rawBits);
    const size_t pixelCount =
        static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < pixelCount; ++i) {
        if ((pixels[i] & 0x00FFFFFFu) != 0) {
            pixels[i] |= 0xFF000000u;
        }
    }

    const size_t maskStride =
        static_cast<size_t>(((width + 15) / 16) * 2);
    std::vector<BYTE> maskBits(maskStride * static_cast<size_t>(height), 0);
    HBITMAP maskBitmap = CreateBitmap(
        width, height, 1, 1, maskBits.data());
    if (!maskBitmap) {
        DeleteObject(colorBitmap);
        return nullptr;
    }

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = colorBitmap;
    info.hbmMask = maskBitmap;
    HICON icon = CreateIconIndirect(&info);

    DeleteObject(maskBitmap);
    DeleteObject(colorBitmap);
    return icon;
}

void EnsureTaskbarGlyphIcons() {
    if (!g_taskbarPlayIcon) {
        g_taskbarPlayIcon = CreateTaskbarGlyphIcon(TaskbarGlyph::Play);
    }
    if (!g_taskbarPauseIcon) {
        g_taskbarPauseIcon = CreateTaskbarGlyphIcon(TaskbarGlyph::Pause);
    }
    if (!g_taskbarPreviousIcon) {
        g_taskbarPreviousIcon = CreateTaskbarGlyphIcon(TaskbarGlyph::Previous);
    }
    if (!g_taskbarStopIcon) {
        g_taskbarStopIcon = CreateTaskbarGlyphIcon(TaskbarGlyph::Stop);
    }
    if (!g_taskbarNextIcon) {
        g_taskbarNextIcon = CreateTaskbarGlyphIcon(TaskbarGlyph::Next);
    }
    if (!g_taskbarFullscreenIcon) {
        g_taskbarFullscreenIcon = CreateTaskbarGlyphIcon(TaskbarGlyph::Fullscreen);
    }
}

void DestroyTaskbarGlyphIcons() {
    HICON* icons[] = {
        &g_taskbarPlayIcon, &g_taskbarPauseIcon, &g_taskbarPreviousIcon,
        &g_taskbarStopIcon, &g_taskbarNextIcon, &g_taskbarFullscreenIcon
    };
    for (HICON* icon : icons) {
        if (*icon) {
            DestroyIcon(*icon);
            *icon = nullptr;
        }
    }
}

void ReleaseTaskbarInterface() {
    if (g_taskbarList) {
        g_taskbarList->Release();
        g_taskbarList = nullptr;
    }
    g_taskbarButtonAdded = false;
    g_taskbarStateKnown = false;
}

void BuildTaskbarButtons(std::array<THUMBBUTTON, kTaskbarButtonCount>& buttons,
                         bool idle, bool paused) {
    EnsureTaskbarGlyphIcons();

    const THUMBBUTTONFLAGS mediaFlags = idle ? THBF_DISABLED : THBF_ENABLED;
    const THUMBBUTTONFLAGS playFlags =
        (!idle || (g_userStoppedPlayback && !g_currentMediaPath.empty()))
            ? THBF_ENABLED
            : THBF_DISABLED;

    auto setButton = [](THUMBBUTTON& button, UINT id, HICON icon,
                        const wchar_t* tooltip, THUMBBUTTONFLAGS flags) {
        button = THUMBBUTTON{};
        button.dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        button.iId = id;
        button.hIcon = icon;
        wcscpy_s(button.szTip, ARRAYSIZE(button.szTip), tooltip);
        button.dwFlags = flags;
    };

    setButton(buttons[0], kTaskbarPreviousButtonId, g_taskbarPreviousIcon,
              L"Previous item",
              g_taskbarPlaylistControls ? mediaFlags : THBF_HIDDEN);
    setButton(buttons[1], kTaskbarStopButtonId, g_taskbarStopIcon,
              L"Stop",
              g_showStopButton ? mediaFlags : THBF_HIDDEN);
    setButton(buttons[2], kTaskbarPlayPauseButtonId,
              (!idle && !paused) ? g_taskbarPauseIcon : g_taskbarPlayIcon,
              (!idle && !paused) ? L"Pause" : L"Play",
              playFlags);
    setButton(buttons[3], kTaskbarNextButtonId, g_taskbarNextIcon,
              L"Next item",
              g_taskbarPlaylistControls ? mediaFlags : THBF_HIDDEN);
    setButton(buttons[4], kTaskbarFullscreenButtonId, g_taskbarFullscreenIcon,
              L"Fullscreen",
              static_cast<THUMBBUTTONFLAGS>(mediaFlags | THBF_DISMISSONCLICK));
}

void UpdateTaskbarButtons(bool idle, bool paused, bool force = false) {
    if (!g_taskbarList || !g_taskbarButtonAdded || !g_main) return;
    if (!force && g_taskbarStateKnown &&
        g_taskbarLastIdle == idle &&
        g_taskbarLastPaused == paused &&
        g_taskbarLastPlaylistControls == g_taskbarPlaylistControls &&
        g_taskbarLastShowStopButton == g_showStopButton) {
        return;
    }

    std::array<THUMBBUTTON, kTaskbarButtonCount> buttons{};
    BuildTaskbarButtons(buttons, idle, paused);

    if (SUCCEEDED(g_taskbarList->ThumbBarUpdateButtons(
            g_main, static_cast<UINT>(buttons.size()), buttons.data()))) {
        g_taskbarLastIdle = idle;
        g_taskbarLastPaused = paused;
        g_taskbarLastPlaylistControls = g_taskbarPlaylistControls;
        g_taskbarLastShowStopButton = g_showStopButton;
        g_taskbarStateKnown = true;
    }
}

void InitializeTaskbarButton() {
    if (!g_main) return;

    ReleaseTaskbarInterface();

    ITaskbarList3* taskbar = nullptr;
    const HRESULT created = CoCreateInstance(
        CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&taskbar));
    if (FAILED(created) || !taskbar) return;

    if (FAILED(taskbar->HrInit())) {
        taskbar->Release();
        return;
    }

    g_taskbarList = taskbar;
    const bool idle = !g_mpvReady || MpvGetFlag("idle-active", true);
    const bool paused = !idle && MpvGetFlag("pause", true);

    std::array<THUMBBUTTON, kTaskbarButtonCount> buttons{};
    BuildTaskbarButtons(buttons, idle, paused);

    // Register all five slots once. The default Simple layout shows Play/Pause
    // and Fullscreen while Previous/Stop/Next are hidden as appropriate. This
    // lets Options switch layouts later with ThumbBarUpdateButtons without
    // re-registering the thumbbar.
    if (SUCCEEDED(g_taskbarList->ThumbBarAddButtons(
            g_main, static_cast<UINT>(buttons.size()), buttons.data()))) {
        g_taskbarButtonAdded = true;
        g_taskbarLastIdle = idle;
        g_taskbarLastPaused = paused;
        g_taskbarLastPlaylistControls = g_taskbarPlaylistControls;
        g_taskbarLastShowStopButton = g_showStopButton;
        g_taskbarStateKnown = true;
    }
}

void PollMpv() {
    if (!g_mpvReady) return;

    bool fileLoaded = false;

    // libmpv requires clients to drain the event queue even when most events
    // are not otherwise consumed. FILE_LOADED is useful here because it is the
    // point at which the new file's track-list is ready for remembered choices.
    if (g_mpvApi.wait_event) {
        for (;;) {
            mpv_event* event = g_mpvApi.wait_event(g_mpv, 0.0);
            if (!event || event->event_id == MPV_EVENT_NONE) break;
            if (event->event_id == MPV_EVENT_FILE_LOADED) {
                fileLoaded = true;
            }
        }
    }

    if (fileLoaded) {
        g_fileLoadPending = false;

        // FILE_LOADED can be caused by mpv's own playlist auto-advance, which
        // bypasses LoadPlaylist(). Save the cached position for the file we are
        // leaving before replacing g_currentMediaPath with the new one.
        SaveCurrentResumePosition();

        const std::string loadedPathUtf8 = MpvGetString("path");
        if (!loadedPathUtf8.empty()) {
            g_currentMediaPath = AbsoluteLocalPath(Utf8ToWide(loadedPathUtf8));
            SetMediaWindowTitle(g_currentMediaPath);

            // Keep the Cutter's single-input source synchronized with whatever
            // mpv is actually showing. Playback navigation can change the active
            // file without going through the explicit OPEN path, so conversion
            // must follow the file that is actually on screen.
            if (g_cutInputs.size() <= 1) {
                const bool inputChanged =
                    g_cutInputs.empty() ||
                    _wcsicmp(g_cutInputs.front().c_str(), g_currentMediaPath.c_str()) != 0;
                if (inputChanged) {
                    g_cutInputs.assign(1, g_currentMediaPath);
                    ResetCutterSelection(false);
                    if (g_cutOpen) InvalidateRect(g_cutOpen, nullptr, FALSE);
    if (g_cutMulti) InvalidateRect(g_cutMulti, nullptr, FALSE);
                }
            }
        }

        // Sync offsets are intentionally per-file. If the user adjusted one on
        // the previous file, return it to neutral when a new file is loaded.
        if (g_audioDelayTouched) {
            MpvSetDouble("audio-delay", 0.0);
            g_audioDelayTouched = false;
        }
        if (g_subtitleDelayTouched) {
            MpvSetDouble("sub-delay", 0.0);
            g_subtitleDelayTouched = false;
        }

        g_lastEofReached = false;
        g_seenPlayingSinceLoad = false;
        g_suppressNextEofFullscreenExit = false;
        ResetKeyframeIndexCache();
        g_chapterTimes.clear();
        if (g_seek) InvalidateRect(g_seek, nullptr, FALSE);

        ApplyRememberedTrackSelections();
        RefreshChapterMarkers();
        if (CutterHasActiveSubtitleSyncTarget()) {
            CutterSelectSubtitleForPreview(
                g_cutSubtitlePath);
        } else {
            CutterApplySubtitlePreviewDelay();
        }

        const bool skipResumeForThisLoad =
            !g_skipResumeOnceForPath.empty() &&
            _wcsicmp(g_skipResumeOnceForPath.c_str(),
                     g_currentMediaPath.c_str()) == 0;
        g_skipResumeOnceForPath.clear();
        g_userStoppedPlayback = false;
        g_userStoppedPlaylistPos = -1;
        if (!skipResumeForThisLoad) {
            RestoreCurrentResumePosition();
        }
        g_pollCounter = 3;

        const int64_t playlistCount = MpvGetInt64("playlist-count", 0);
        const int64_t playlistPos = MpvGetInt64("playlist-pos", -1);
        const bool playlistTransition =
            playlistCount > 1 && playlistPos >= 0 &&
            g_haveLoadedPlaylistItem && playlistPos != g_lastLoadedPlaylistPos;

        if (playlistPos >= 0) {
            g_lastLoadedPlaylistPos = playlistPos;
            g_haveLoadedPlaylistItem = true;
        }

        if (g_playlistOsdVisible) {
            // Refresh the selected/gold entry after mpv advances internally.
            ShowPlaylistOsd();
        } else if (playlistTransition) {
            // Optional, automatic feedback when playback moves to another item.
            // The persistent P-toggle remains independent of this preference.
            switch (g_playlistStartOsd) {
            case PlaylistStartOsd::Title:
                ShowOsdText(CurrentMediaTitleForOsd(), 2000);
                break;
            case PlaylistStartOsd::Playlist:
                FlashPlaylistOsd();
                break;
            case PlaylistStartOsd::Nothing:
            default:
                break;
            }
        }
    }

    const double oldTime = g_timePos;
    const double oldDuration = g_duration;
    const int oldVolume = g_volumePercent;
    const bool oldMuted = g_muted;
    const bool oldPlaying = g_playing;

    g_timePos = std::max(0.0, MpvGetDouble("time-pos", 0.0));
    g_duration = std::max(0.0, MpvGetDouble("duration", 0.0));
    if (g_cutSubtitleDriftEnabled &&
        CutterHasActiveSubtitleSyncTarget() &&
        CutterSubtitleDriftReady()) {
        const int delayMs =
            CutterSubtitlePreviewDelayMsAt(g_timePos);
        const double delaySeconds =
            static_cast<double>(delayMs) / 1000.0;
        const double currentDelay =
            MpvGetDouble("sub-delay", 0.0);
        if (std::abs(currentDelay - delaySeconds) > 0.002) {
            MpvSetDouble("sub-delay", delaySeconds);
            g_subtitleDelayTouched = delayMs != 0;
        }
    }
    if (g_cutMarkBAutoEnd && g_cutMarkA >= 0.0 && g_duration > g_cutMarkA) {
        g_cutMarkB = g_duration;
    }
    g_volumePercent = std::clamp(static_cast<int>(MpvGetDouble("volume", g_volumePercent) + 0.5), 0, 100);
    g_muted = MpvGetFlag("mute", false);

    const bool idle = MpvGetFlag("idle-active", true);
    const bool paused = MpvGetFlag("pause", true);
    const bool eofReached = MpvGetFlag("eof-reached", false);
    g_playing = !idle && !paused;

    if (g_playing) {
        g_seenPlayingSinceLoad = true;
    }

    if (g_suppressNextEofFullscreenExit &&
        g_duration > 0.0 &&
        g_timePos < g_duration - 0.25) {
        // A deliberate seek onto EOF may take a poll or two before mpv reports
        // eof-reached. Keep only the fullscreen-exit suppression armed until
        // the playhead moves materially back into the file.
        g_suppressNextEofFullscreenExit = false;
    }

    const bool eofJustReached =
        eofReached &&
        !g_lastEofReached &&
        g_seenPlayingSinceLoad;

    // LOOP current-file takes priority over playlist/AUTO advancement and the
    // final-EOF fullscreen-exit policy. libmpv owns the actual repeat; this
    // guard simply prevents frontend EOF side effects if eof-reached pulses.
    if (eofJustReached && !g_loopCurrentFile) {
        const int64_t playlistCount = MpvGetInt64("playlist-count", 0);
        const int64_t playlistPos = MpvGetInt64("playlist-pos", -1);
        const bool playlistHasNext =
            playlistPos >= 0 &&
            playlistCount > 0 &&
            playlistPos < playlistCount - 1;

        // AUTO is deliberately suppressed while an explicit multi-item mpv
        // playlist exists, so mirror that same priority rule here. Resolve the
        // next folder item once and reuse that exact path for AUTO advance.
        const bool autoCanAdvance =
            g_autoPlayNextFile &&
            !g_fileLoadPending &&
            playlistCount <= 1 &&
            !g_currentMediaPath.empty();

        std::wstring nextAutoPath;
        if (autoCanAdvance) {
            nextAutoPath = FindNextMediaFileInFolder(g_currentMediaPath);
        }

        const bool nextThingExists =
            playlistHasNext || !nextAutoPath.empty();
        // AUTO should advance whether EOF was reached naturally or by a
        // deliberate seek. Only the optional fullscreen-exit behavior needs
        // to distinguish those two cases.
        if (g_exitFullscreenOnPlaybackEnd &&
            g_fullscreen &&
            !g_suppressNextEofFullscreenExit &&
            !nextThingExists) {
            ExitFullscreen();
        }

        if (!nextAutoPath.empty()) {
            g_pendingAutoNextPath = nextAutoPath;
            if (!PostMessageW(g_main, WM_APP_AUTO_NEXT_FILE, 0, 0)) {
                g_pendingAutoNextPath.clear();
            }
        }
    }
    g_lastEofReached = eofReached;

    if (oldTime != g_timePos || oldDuration != g_duration) {
        UpdateTimeLabel();
        if (g_seek) InvalidateRect(g_seek, nullptr, FALSE);
    }
    if (oldVolume != g_volumePercent) {
        if (g_volume) InvalidateRect(g_volume, nullptr, FALSE);
        UpdateVolumeLabel();
    }
    if (oldMuted != g_muted && g_mute) InvalidateRect(g_mute, nullptr, FALSE);
    if (oldPlaying != g_playing && g_play) InvalidateRect(g_play, nullptr, FALSE);

    UpdateTaskbarButtons(idle, paused);

    if (g_overlay) SetWindowPos(g_overlay, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (g_cropOverlay) SetWindowPos(g_cropOverlay, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    if (++g_pollCounter >= 4) {
        g_pollCounter = 0;
        UpdateStatusText();
        RefreshChapterMarkers();
    }
}

bool InitMpv() {
    const std::wstring exeDir = GetExeDirectory();
    const std::wstring libMpvDir = exeDir + L"\\libmpv";
    const std::wstring dllPath = libMpvDir + L"\\libmpv-2.dll";

    // Detect a missing engine before configuring the DLL search path so the
    // user gets a useful error instead of a generic loader failure.
    if (!FileExists(dllPath)) {
        g_statusText = L"libmpv runtime missing";
        if (g_status) SetWindowTextW(g_status, g_statusText.c_str());
        const std::wstring message =
            L"The playback engine is missing.\n\n"
            L"Expected: libmpv\\libmpv-2.dll\n\n"
            L"Reinstall the Full Portable package, or place a compatible "
            L"libmpv runtime in the libmpv folder.\n\n"
            L"Project: " + std::wstring(kProjectUrl);
        MessageBoxW(
            g_main, message.c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
        return false;
    }

    // Keep the playback engine isolated from the frontend. Register libmpv\
    // as a dedicated DLL directory for the process lifetime so both normal
    // imports and libraries that libmpv loads dynamically (Vulkan/EGL/etc.)
    // can resolve from the engine folder without placing runtime DLLs beside
    // the frontend EXE.
    if (!SetDefaultDllDirectories(
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        g_statusText = L"DLL search setup failed";
        if (g_status) SetWindowTextW(g_status, g_statusText.c_str());
        MessageBoxW(g_main,
                    L"Windows could not configure the isolated libmpv DLL search path.",
                    kAppTitle, MB_OK | MB_ICONERROR);
        return false;
    }

    g_libMpvDllDirectoryCookie = AddDllDirectory(libMpvDir.c_str());
    if (!g_libMpvDllDirectoryCookie) {
        g_statusText = L"libmpv folder unavailable";
        if (g_status) SetWindowTextW(g_status, g_statusText.c_str());
        MessageBoxW(g_main,
                    L"Windows could not register the libmpv runtime folder.\n\n"
                    L"Expected folder: libmpv\\",
                    kAppTitle, MB_OK | MB_ICONERROR);
        return false;
    }

    g_mpvApi.dll = LoadLibraryExW(
        dllPath.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
            LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (!g_mpvApi.dll) {
        g_statusText = L"libmpv runtime load failed";
        if (g_status) SetWindowTextW(g_status, g_statusText.c_str());
        MessageBoxW(g_main,
                    L"The libmpv runtime was found but could not be loaded.\n\n"
                    L"Use the runtime supplied with the WinterStatic Video Cutter Full Portable package, or replace the entire libmpv folder with a compatible runtime.",
                    kAppTitle, MB_OK | MB_ICONWARNING);
        RemoveDllDirectory(g_libMpvDllDirectoryCookie);
        g_libMpvDllDirectoryCookie = nullptr;
        return false;
    }

    bool ok = true;
    ok &= LoadProc(g_mpvApi.create, "mpv_create");
    ok &= LoadProc(g_mpvApi.initialize, "mpv_initialize");
    ok &= LoadProc(g_mpvApi.terminate_destroy, "mpv_terminate_destroy");
    ok &= LoadProc(g_mpvApi.command, "mpv_command");
    ok &= LoadProc(g_mpvApi.command_node, "mpv_command_node");
    ok &= LoadProc(g_mpvApi.set_option, "mpv_set_option");
    ok &= LoadProc(g_mpvApi.set_option_string, "mpv_set_option_string");
    ok &= LoadProc(g_mpvApi.load_config_file, "mpv_load_config_file");
    ok &= LoadProc(g_mpvApi.get_property, "mpv_get_property");
    ok &= LoadProc(g_mpvApi.set_property, "mpv_set_property");
    ok &= LoadProc(g_mpvApi.set_property_string, "mpv_set_property_string");
    ok &= LoadProc(g_mpvApi.wait_event, "mpv_wait_event");
    ok &= LoadProc(g_mpvApi.free_fn, "mpv_free");
    ok &= LoadProc(g_mpvApi.free_node_contents, "mpv_free_node_contents");
    LoadProc(g_mpvApi.error_string, "mpv_error_string");
    if (!ok) {
        MessageBoxW(g_main, L"The installed libmpv-2.dll is missing one or more required client API exports.", kAppTitle, MB_OK | MB_ICONERROR);
        return false;
    }

    g_mpv = g_mpvApi.create();
    if (!g_mpv) {
        MessageBoxW(g_main, L"mpv_create() failed.", kAppTitle, MB_OK | MB_ICONERROR);
        return false;
    }

    // Ignore standalone mpv config locations. This frontend loads only the
    // frontend-owned mpv.conf beside WinterStatic-Video-Cutter.exe.
    g_mpvApi.set_option_string(g_mpv, "config", "no");

    // Baseline playback defaults. Local mpv.conf is loaded afterwards so it
    // can override playback choices such as hwdec, vo, gpu-api, scaling, etc.
    g_mpvApi.set_option_string(g_mpv, "keep-open", "yes");
    g_mpvApi.set_option_string(g_mpv, "hwdec", "auto-safe");

    const std::wstring mpvConfigPath = GetMpvConfigPath();
    if (FileExists(mpvConfigPath)) {
        const std::string configUtf8 = WideToUtf8(mpvConfigPath);
        const int configResult =
            g_mpvApi.load_config_file(g_mpv, configUtf8.c_str());

        if (configResult < 0) {
            const std::wstring msg =
                L"mpv.conf could not be loaded:\n\n" + MpvError(configResult);
            MessageBoxW(g_main, msg.c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
        }
    }

    // The Options-dialog GPU API selector is authoritative and is applied
    // after mpv.conf. Changing it requires restarting the player.
    g_mpvApi.set_option_string(g_mpv, "gpu-api", GpuApiOptionValue(g_gpuApi));

    // Frontend-owned options are applied after mpv.conf so custom playback
    // tuning cannot accidentally re-enable mpv's OSC or keyboard input.
    // Reassert config=no so no separate standalone-mpv config is imported.
    g_mpvApi.set_option_string(g_mpv, "config", "no");
    g_mpvApi.set_option_string(g_mpv, "terminal", "no");
    g_mpvApi.set_option_string(g_mpv, "osc", "no");
    g_mpvApi.set_option_string(g_mpv, "load-stats-overlay", "yes");
    g_mpvApi.set_option_string(g_mpv, "input-default-bindings", "no");
    g_mpvApi.set_option_string(g_mpv, "input-vo-keyboard", "no");
    g_mpvApi.set_option_string(g_mpv, "volume-max", "100");
    g_mpvApi.set_option_string(g_mpv, "loop-file", "no");
    if (!g_preferredAudioLanguage.empty()) {
        const std::string alang = WideToUtf8(g_preferredAudioLanguage);
        g_mpvApi.set_option_string(g_mpv, "alang", alang.c_str());
    }
    if (!g_preferredSubtitleLanguage.empty()) {
        const std::string slang = WideToUtf8(g_preferredSubtitleLanguage);
        g_mpvApi.set_option_string(g_mpv, "slang", slang.c_str());
    }
    const std::string subtitleSize = std::to_string(g_subtitleFontSize);
    g_mpvApi.set_option_string(g_mpv, "sub-font-size", subtitleSize.c_str());

    g_mpvApi.set_option_string(g_mpv, "osd-align-x", "center");
    g_mpvApi.set_option_string(g_mpv, "osd-align-y", "top");
    g_mpvApi.set_option_string(g_mpv, "osd-margin-y", "180");
    g_mpvApi.set_option_string(g_mpv, "osd-font", "Segoe UI Semibold");
    const std::string osdSize = std::to_string(g_osdFontSize);
    g_mpvApi.set_option_string(g_mpv, "osd-font-size", osdSize.c_str());
    g_mpvApi.set_option_string(g_mpv, "osd-bold", "yes");
    g_mpvApi.set_option_string(g_mpv, "osd-border-size", "1.2");
    g_mpvApi.set_option_string(g_mpv, "osd-shadow-offset", "1.5");

    // On Windows, --wid expects the HWND value as an unsigned 32-bit value.
    const uint32_t rawWid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_video));
    int64_t wid = static_cast<int64_t>(rawWid);
    g_mpvApi.set_option(g_mpv, "wid", MPV_FORMAT_INT64, &wid);

    const int init = g_mpvApi.initialize(g_mpv);
    if (init < 0) {
        const std::wstring msg = L"libmpv failed to initialize:\n\n" + MpvError(init);
        MessageBoxW(g_main, msg.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
        g_mpvApi.terminate_destroy(g_mpv);
        g_mpv = nullptr;
        return false;
    }

    g_mpvReady = true;
    MpvSetDouble("volume", static_cast<double>(g_volumePercent));
    MpvSetFlag("mute", g_muted);
    UpdateStatusText();
    return true;
}

void ShutdownMpv() {
    g_mpvReady = false;
    if (g_mpv && g_mpvApi.terminate_destroy) {
        g_mpvApi.terminate_destroy(g_mpv);
        g_mpv = nullptr;
    }
    if (g_mpvApi.dll) {
        FreeLibrary(g_mpvApi.dll);
        g_mpvApi.dll = nullptr;
    }
    if (g_libMpvDllDirectoryCookie) {
        RemoveDllDirectory(g_libMpvDllDirectoryCookie);
        g_libMpvDllDirectoryCookie = nullptr;
    }
}

std::wstring DisplayNameFromPath(const std::wstring& path) {
    if (path.empty()) return kAppTitle;
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    return name.empty() ? kAppTitle : name;
}

void SetMediaWindowTitle(const std::wstring& path) {
    if (!g_main) return;
    if (path.empty()) {
        SetWindowTextW(g_main, kAppTitle);
        return;
    }
    const std::wstring fileName = DisplayNameFromPath(path);
    const std::wstring title = fileName + L" - " + kAppTitle;
    SetWindowTextW(g_main, title.c_str());
}

std::wstring CurrentMediaTitleForOsd() {
    const std::string mpvTitle = MpvGetString("media-title");
    if (!mpvTitle.empty()) return Utf8ToWide(mpvTitle);
    return DisplayNameFromPath(g_currentMediaPath);
}

bool IsSupportedMediaFile(const std::filesystem::path& path) {
    const std::wstring ext = path.extension().wstring();
    static constexpr const wchar_t* kExtensions[] = {
        L".mkv", L".mp4", L".avi", L".mov", L".wmv", L".webm",
        L".m4v", L".mpg", L".mpeg", L".ts", L".m2ts",
        L".mp3", L".flac", L".wav", L".m4a", L".aac", L".ogg", L".opus"
    };

    for (const wchar_t* supported : kExtensions) {
        if (_wcsicmp(ext.c_str(), supported) == 0) return true;
    }
    return false;
}

std::wstring AbsoluteLocalPath(const std::wstring& path) {
    std::error_code ec;
    const std::filesystem::path absolute =
        std::filesystem::absolute(std::filesystem::path(path), ec);
    return ec ? path : absolute.lexically_normal().wstring();
}

std::wstring FindAdjacentMediaFileInFolder(const std::wstring& currentPath,
                                             bool next) {
    if (currentPath.empty()) return L"";

    const std::filesystem::path current(currentPath);
    std::filesystem::path directory = current.parent_path();
    if (directory.empty()) directory = L".";

    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(directory, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        std::error_code typeError;
        if (!it->is_regular_file(typeError) || typeError) continue;
        if (IsSupportedMediaFile(it->path())) {
            files.push_back(it->path());
        }
    }
    if (ec || files.empty()) return L"";

    std::sort(files.begin(), files.end(),
              [](const std::filesystem::path& a,
                 const std::filesystem::path& b) {
        const std::wstring an = a.filename().wstring();
        const std::wstring bn = b.filename().wstring();
        const int logical = StrCmpLogicalW(an.c_str(), bn.c_str());
        if (logical != 0) return logical < 0;
        return _wcsicmp(a.wstring().c_str(), b.wstring().c_str()) < 0;
    });

    const std::wstring currentName = current.filename().wstring();
    for (size_t i = 0; i < files.size(); ++i) {
        if (_wcsicmp(files[i].filename().wstring().c_str(),
                     currentName.c_str()) != 0) {
            continue;
        }

        if (next) {
            return i + 1 < files.size() ? files[i + 1].wstring() : L"";
        }
        return i > 0 ? files[i - 1].wstring() : L"";
    }

    return L"";
}

std::wstring FindNextMediaFileInFolder(const std::wstring& currentPath) {
    return FindAdjacentMediaFileInFolder(currentPath, true);
}

void LoadPlaylist(const std::vector<std::wstring>& paths) {
    if (!g_mpvReady || paths.empty()) return;

    // User-managed external subtitle rows can be prepared before a video is
    // loaded. Preserve them across source replacement; embedded rows and
    // auto-paired sidecars are rebuilt for the new primary input.
    StoreLegacySubtitleSyncToManaged();

    g_cutInputs.clear();
    for (const auto& rawPath : paths) {
        if (!rawPath.empty()) g_cutInputs.push_back(AbsoluteLocalPath(rawPath));
    }
    ResetCutterSelection(false);
    ResetCutterManagedAudioTracksForSource();
    ResetCutterManagedSubtitleTracksForSource(true);
    if (g_cutInputs.size() == 1) {
        CutterAutoPairSubtitleForInput(g_cutInputs.front());
    }
    if (g_cutOpen) InvalidateRect(g_cutOpen, nullptr, FALSE);
    if (g_cutMulti) InvalidateRect(g_cutMulti, nullptr, FALSE);

    // An explicit load is unrelated to a previous user Stop. Resume handling
    // should therefore use the normal per-file rules for the newly opened item.
    g_userStoppedPlayback = false;
    g_userStoppedPlaylistPos = -1;
    g_skipResumeOnceForPath.clear();

    // Any explicit/replacement load supersedes a queued AUTO transition.
    g_pendingAutoNextPath.clear();

    // Preserve the current file before a replace load. Natural mpv playlist
    // advances are covered separately by FILE_LOADED in PollMpv().
    SaveCurrentResumePosition();

    if (!g_preferredAudioLanguage.empty()) {
        MpvSetString("alang", g_preferredAudioLanguage);
    }
    if (!g_preferredSubtitleLanguage.empty()) {
        MpvSetString("slang", g_preferredSubtitleLanguage);
    }

    g_fileLoadPending = true;
    // A newly opened/replaced playlist starts a fresh transition history.
    // The first file is not treated as "the next video" for automatic OSD.
    g_haveLoadedPlaylistItem = false;
    g_lastLoadedPlaylistPos = -1;

    bool first = true;
    for (const auto& rawPath : paths) {
        if (rawPath.empty()) continue;
        const std::wstring path = AbsoluteLocalPath(rawPath);
        const std::string utf8 = WideToUtf8(path);
        if (utf8.empty()) continue;

        if (first) {
            if (!MpvCommand({"loadfile", utf8, "replace"})) {
                g_fileLoadPending = false;
                return;
            }
            first = false;
        } else {
            MpvCommand({"loadfile", utf8, "append"});
        }
    }

    if (first) {
        g_fileLoadPending = false;
        return;
    }

    // An explicit/replacement open should behave like opening a new video, not
    // inherit the pause state of the file it replaced. This covers File -> Open,
    // drag/drop, Explorer/existing-instance handoff, startup paths, and manual
    // AUTO folder navigation. Natural mpv playlist advances are unaffected.
    MpvSetFlag("pause", false);
    g_pollCounter = 3;
}

void LoadFile(const std::wstring& path) {
    if (path.empty()) return;
    LoadPlaylist({ path });
}

void PlayPendingAutoNextFile() {
    // The EOF transition already resolved the next file. Consume that cached
    // path here instead of enumerating/sorting the same folder a second time.
    const std::wstring next = g_pendingAutoNextPath;
    g_pendingAutoNextPath.clear();

    if (!g_autoPlayNextFile || next.empty()) return;
    if (MpvGetInt64("playlist-count", 0) > 1) return;

    LoadFile(next);
}

void ToggleAutoPlayNextFile() {
    g_autoPlayNextFile = !g_autoPlayNextFile;
    WriteSettingInt(L"General", L"AutoPlayNextFile", 0);
    if (g_autoNext) InvalidateRect(g_autoNext, nullptr, FALSE);
}

void ToggleLoopCurrentFile() {
    if (!g_mpvReady) return;

    g_loopCurrentFile = !g_loopCurrentFile;
    MpvCommand({"set", "loop-file", g_loopCurrentFile ? "inf" : "no"});

    if (g_loopCurrent) InvalidateRect(g_loopCurrent, nullptr, FALSE);
    if (g_playMenu) {
        CheckMenuItem(g_playMenu, IDM_LOOP_CURRENT_FILE,
                      MF_BYCOMMAND |
                      (g_loopCurrentFile ? MF_CHECKED : MF_UNCHECKED));
    }
    ShowOsdText(g_loopCurrentFile ? L"Loop: On" : L"Loop: Off", 1000);
}

std::vector<std::wstring> ChooseMediaFiles(HWND owner) {
    // Explorer-style multi-select returns either one full path, or a directory
    // followed by one or more file names, terminated by a second NUL.
    std::vector<wchar_t> buffer(65536, L'\0');
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = owner ? owner : g_main;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrFilter =
        L"Media files\0*.mkv;*.mp4;*.avi;*.mov;*.wmv;*.webm;*.m4v;*.mpg;*.mpeg;*.ts;*.m2ts;*.mp3;*.flac;*.wav;*.m4a;*.aac;*.ogg;*.opus\0"
        L"All files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_ALLOWMULTISELECT;

    if (!GetOpenFileNameW(&ofn)) return {};

    std::vector<std::wstring> paths;
    const wchar_t* first = buffer.data();
    const wchar_t* next = first + wcslen(first) + 1;

    if (*next == L'\0') {
        paths.emplace_back(first);
    } else {
        const std::filesystem::path directory(first);
        for (const wchar_t* name = next; *name; name += wcslen(name) + 1) {
            paths.push_back((directory / name).wstring());
        }
    }
    return paths;
}

std::vector<std::wstring> ChooseAudioFiles(HWND owner) {
    std::vector<wchar_t> buffer(65536, L'\0');
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = owner ? owner : g_main;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrFilter =
        L"Audio files\0*.mp3;*.flac;*.wav;*.m4a;*.aac;*.ogg;*.opus;*.ac3;*.eac3;*.mka;*.wma;*.aiff;*.aif\0"
        L"All files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                OFN_EXPLORER | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&ofn)) return {};

    std::vector<std::wstring> paths;
    const wchar_t* first = buffer.data();
    const wchar_t* next = first + wcslen(first) + 1;
    if (*next == L'\0') {
        paths.push_back(AbsoluteLocalPath(first));
    } else {
        const std::filesystem::path directory(first);
        for (const wchar_t* name = next; *name; name += wcslen(name) + 1) {
            paths.push_back(AbsoluteLocalPath((directory / name).wstring()));
        }
    }
    return paths;
}

std::vector<std::wstring> ChooseSubtitleFiles(HWND owner) {
    std::vector<wchar_t> buffer(65536, L'\0');
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = owner ? owner : g_main;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrFilter =
        L"Subtitle files\0*.srt;*.ass;*.ssa;*.vtt;*.sub\0"
        L"All files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                OFN_EXPLORER | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&ofn)) return {};

    std::vector<std::wstring> paths;
    const wchar_t* first = buffer.data();
    const wchar_t* next = first + wcslen(first) + 1;
    if (*next == L'\0') {
        paths.push_back(AbsoluteLocalPath(first));
    } else {
        const std::filesystem::path directory(first);
        for (const wchar_t* name = next; *name; name += wcslen(name) + 1) {
            paths.push_back(AbsoluteLocalPath((directory / name).wstring()));
        }
    }
    return paths;
}

std::wstring ChooseSubtitleFile(HWND owner) {
    const auto paths = ChooseSubtitleFiles(owner);
    return paths.empty() ? L"" : paths.front();
}

void OpenFileDialog() {
    const auto paths = ChooseMediaFiles(g_main);
    if (!paths.empty()) LoadPlaylist(paths);
}

void TogglePlay() {
    if (!g_mpvReady) return;
    const bool paused = MpvGetFlag("pause", false);
    const bool idle = MpvGetFlag("idle-active", true);

    if (idle) {
        // Stop enters mpv idle mode. `stop keep-playlist` deliberately keeps the
        // authoritative libmpv playlist, but after stopping there is not always
        // an active "current" entry. Restart the exact numeric playlist item we
        // cached before Stop instead of relying on `playlist-play-index current`.
        if (!g_userStoppedPlayback || g_currentMediaPath.empty()) return;

        int64_t restartPos = g_userStoppedPlaylistPos;
        const auto entries = GetPlaylistEntries();
        if (restartPos < 0 || restartPos >= static_cast<int64_t>(entries.size())) {
            // Defensive fallback: locate the stopped file in the retained mpv
            // playlist if this libmpv build did not report playlist-pos reliably.
            restartPos = -1;
            for (size_t i = 0; i < entries.size(); ++i) {
                const std::wstring entryPath =
                    AbsoluteLocalPath(Utf8ToWide(entries[i].filename));
                if (!entryPath.empty() &&
                    _wcsicmp(entryPath.c_str(), g_currentMediaPath.c_str()) == 0) {
                    restartPos = static_cast<int64_t>(i);
                    break;
                }
            }
        }
        if (restartPos < 0) return;

        g_skipResumeOnceForPath = g_currentMediaPath;
        if (!MpvCommand({"playlist-play-index", std::to_string(restartPos)})) {
            g_skipResumeOnceForPath.clear();
            return;
        }
        g_userStoppedPlaylistPos = -1;
        MpvSetFlag("pause", false);
        g_keyframeNavIndex = -1;
        PollMpv();
        ShowOsdText(L"\u25B6", 900);
        return;
    }

    const bool newPaused = !paused;
    if (!MpvSetFlag("pause", newPaused)) return;
    if (!newPaused) {
        g_keyframeNavIndex = -1;
    }
    PollMpv();

    // Brief old-player/VHS-style confirmation for explicit Play/Pause actions.
    // This uses the normal transient OSD path (Top Left by default), so every
    // input route through TogglePlay() gets the same feedback without changing
    // playback behavior or creating a separate overlay.
    ShowOsdText(newPaused ? L"\u275A\u275A" : L"\u25B6", 900);
}

void PausePlaybackIfPlaying() {
    if (!g_mpvReady) return;
    if (MpvGetFlag("idle-active", true)) return;
    if (!MpvGetFlag("pause", false)) {
        MpvSetFlag("pause", true);
        PollMpv();
    }
}

void StopPlayback() {
    if (!g_mpvReady || MpvGetFlag("idle-active", true)) return;

    // Remember the exact libmpv playlist item before Stop. `keep-playlist`
    // preserves the queue, but the active/current marker may disappear in idle
    // mode; the cached numeric index gives Play a deterministic restart target.
    int64_t stoppedPos = MpvGetInt64("playlist-pos", -1);
    if (stoppedPos < 0) {
        const auto entries = GetPlaylistEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].playing || entries[i].current) {
                stoppedPos = static_cast<int64_t>(i);
                break;
            }
        }
    }

    if (MpvCommand({"stop", "keep-playlist"})) {
        g_keyframeNavIndex = -1;
        g_userStoppedPlayback = !g_currentMediaPath.empty();
        g_userStoppedPlaylistPos = stoppedPos;
    }
    PollMpv();
}

void SeekAbsolute(double seconds);

void ResetKeyframeIndexCache() {
    g_keyframeIndexPath.clear();
    g_keyframeTimes.clear();
    g_keyframeIndexAttempted = false;
    g_keyframeIndexFailureShown = false;
    g_keyframeIndexError.clear();
    g_keyframeNavIndex = -1;
}

bool ProbeCurrentVideoKeyframes(const std::wstring& rawPath,
                                std::vector<double>& keyframes,
                                std::wstring& errorText) {
    keyframes.clear();
    errorText.clear();

    const std::wstring path = AbsoluteLocalPath(rawPath);
    if (path.empty() || !FileExists(path)) {
        errorText = L"Current video path is unavailable.";
        return false;
    }

    const std::wstring ffprobe = FindCutterTool(L"ffprobe.exe");
    if (ffprobe.empty()) {
        errorText = L"ffprobe.exe was not found.";
        return false;
    }

    // Prefer the exact currently selected mpv video stream when mpv exposes
    // its FFmpeg global stream index. Most files have one video stream, but
    // this keeps the keyframe index tied to what the user is actually viewing.
    std::wstring streamSpec = L"v:0";
    for (const auto& track : GetTracks("video")) {
        if (track.selected && track.ffIndex >= 0) {
            streamSpec = std::to_wstring(track.ffIndex);
            break;
        }
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        errorText = L"Could not create the ffprobe output pipe.";
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nullHandle = CreateFileW(
        L"NUL",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (nullHandle == INVALID_HANDLE_VALUE) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        errorText = L"Could not open the null device for ffprobe.";
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nullHandle;
    si.hStdOutput = writePipe;
    si.hStdError = nullHandle;

    PROCESS_INFORMATION pi{};
    std::wstring command =
        L"\"" + ffprobe +
        L"\" -v error -select_streams " + streamSpec +
        L" -skip_frame nokey -show_frames "
        L"-show_entries frame=best_effort_timestamp_time "
        L"-of csv=p=0 \"" + path + L"\"";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    const BOOL started = CreateProcessW(
        ffprobe.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    CloseHandle(writePipe);
    CloseHandle(nullHandle);
    if (!started) {
        CloseHandle(readPipe);
        errorText = L"ffprobe could not be started.";
        return false;
    }

    std::string output;
    output.reserve(64 * 1024);
    char buffer[4096]{};
    DWORD read = 0;
    constexpr size_t kMaxKeyframeProbeOutput = 8 * 1024 * 1024;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        if (output.size() + read > kMaxKeyframeProbeOutput) {
            output.append(buffer, buffer +
                (kMaxKeyframeProbeOutput - output.size()));
            break;
        }
        output.append(buffer, buffer + read);
    }
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0) {
        errorText = L"ffprobe could not read video keyframes.";
        return false;
    }

    // mpv rebases normal playback time to zero by default. ffprobe reports the
    // container timestamps, so subtract mpv's demuxer start time to put both
    // clocks on the same timeline (important for files such as transport
    // streams whose first timestamp is not zero).
    double demuxerStart = MpvGetDouble("demuxer-start-time", 0.0);
    if (!std::isfinite(demuxerStart)) demuxerStart = 0.0;

    size_t lineStart = 0;
    while (lineStart < output.size()) {
        size_t lineEnd = output.find_first_of("\r\n", lineStart);
        if (lineEnd == std::string::npos) lineEnd = output.size();
        const std::string line = output.substr(lineStart, lineEnd - lineStart);
        if (!line.empty()) {
            char* end = nullptr;
            const double rawTime = std::strtod(line.c_str(), &end);
            if (end && end != line.c_str() && std::isfinite(rawTime)) {
                double t = rawTime - demuxerStart;
                if (t >= -0.050) {
                    if (t < 0.0) t = 0.0;
                    if (g_duration <= 0.0 || t <= g_duration + 0.250) {
                        keyframes.push_back(t);
                    }
                }
            }
        }
        lineStart = lineEnd;
        while (lineStart < output.size() &&
               (output[lineStart] == '\r' || output[lineStart] == '\n')) {
            ++lineStart;
        }
    }

    std::sort(keyframes.begin(), keyframes.end());
    keyframes.erase(
        std::unique(keyframes.begin(), keyframes.end(),
                    [](double a, double b) {
                        return std::abs(a - b) < 0.0005;
                    }),
        keyframes.end());

    if (keyframes.empty()) {
        errorText = L"No video keyframes were returned by ffprobe.";
        return false;
    }
    return true;
}

bool EnsureCurrentKeyframeIndex() {
    const std::wstring path = AbsoluteLocalPath(g_currentMediaPath);
    if (path.empty()) return false;

    if (g_keyframeIndexPath.empty() ||
        _wcsicmp(g_keyframeIndexPath.c_str(), path.c_str()) != 0) {
        ResetKeyframeIndexCache();
        g_keyframeIndexPath = path;
    }

    if (g_keyframeIndexAttempted) return !g_keyframeTimes.empty();
    g_keyframeIndexAttempted = true;
    ShowOsdText(L"Scanning keyframes...", 1500);
    return ProbeCurrentVideoKeyframes(path, g_keyframeTimes,
                                      g_keyframeIndexError);
}

bool SeekAdjacentKeyframe(bool next) {
    if (!EnsureCurrentKeyframeIndex()) return false;

    const double current = std::max(
        0.0, MpvGetDouble("time-pos", g_timePos));
    const auto& frames = g_keyframeTimes;
    if (frames.empty()) return false;

    // Prefer the exact indexed position Cutter selected on the previous
    // adjacent-keyframe command. If another navigation path invalidated that
    // state, retain the 2 ms timestamp matcher as the fallback.
    constexpr double kKeyframeMatchTolerance = 0.002;
    auto lower = std::lower_bound(frames.begin(), frames.end(), current);
    std::ptrdiff_t currentIndex = -1;
    const bool paused = MpvGetFlag("pause", false);

    if (paused &&
        g_keyframeNavIndex >= 0 &&
        g_keyframeNavIndex < static_cast<std::ptrdiff_t>(frames.size())) {
        currentIndex = g_keyframeNavIndex;
    } else {
        if (!paused) {
            g_keyframeNavIndex = -1;
        }
        double bestDiff = kKeyframeMatchTolerance + 1.0;
        if (lower != frames.end()) {
            const double diff = std::abs(*lower - current);
            if (diff <= kKeyframeMatchTolerance) {
                currentIndex = std::distance(frames.begin(), lower);
                bestDiff = diff;
            }
        }
        if (lower != frames.begin()) {
            const auto previous = std::prev(lower);
            const double diff = std::abs(*previous - current);
            if (diff <= kKeyframeMatchTolerance && diff < bestDiff) {
                currentIndex = std::distance(frames.begin(), previous);
            }
        }
    }

    std::ptrdiff_t targetIndex = -1;
    if (currentIndex >= 0) {
        targetIndex = currentIndex + (next ? 1 : -1);
    } else if (next) {
        targetIndex = std::distance(frames.begin(), lower);
    } else {
        if (lower != frames.begin()) {
            targetIndex = std::distance(frames.begin(), std::prev(lower));
        }
    }

    if (targetIndex < 0) {
        SeekAbsolute(0.0);
        return true;
    }
    if (targetIndex >= static_cast<std::ptrdiff_t>(frames.size())) {
        SeekAbsolute(g_duration);
        return true;
    }

    const double target = frames[static_cast<size_t>(targetIndex)];
    char amount[64]{};
    sprintf_s(amount, "%.6f", target);

    // We already know the exact timestamp is a keyframe. Use an exact absolute
    // seek to that timestamp so the demuxer's own approximate keyframe search
    // cannot jump past the indexed frame again.
    if (!MpvCommand({"osd-msg", "seek", amount, "absolute+exact"})) {
        return false;
    }
    g_keyframeNavIndex = paused ? targetIndex : -1;
    g_timePos = target;
    g_pollCounter = 3;

    UpdateTimeLabel();
    if (g_seek) {
        InvalidateRect(g_seek, nullptr, FALSE);
    }

    return true;
}

void SeekBy(int seconds) {
    if (!g_mpvReady || g_duration <= 0.0) return;

    // With STICK KF enabled, Left/Right and the seek-arrow buttons navigate
    // the cached keyframe list one frame at a time. The old +/-5 second
    // relative+keyframes command could legitimately jump across several
    // keyframes whenever more than one lived inside that five-second span.
    if (g_cutStickKeyframes && seconds != 0) {
        if (SeekAdjacentKeyframe(seconds > 0)) return;
        if (!g_keyframeIndexFailureShown) {
            g_keyframeIndexFailureShown = true;
            const std::wstring detail = g_keyframeIndexError.empty()
                ? L"Keyframe index unavailable"
                : L"Keyframe index unavailable: " + g_keyframeIndexError;
            ShowOsdText(detail + L"; using normal 5 s seek", 2200);
        }
    }

    // Any relative seek that reaches this path is outside indexed adjacent
    // navigation, so the remembered keyframe position is no longer authoritative.
    g_keyframeNavIndex = -1;

    // A relative keyframe seek that runs past EOF can stop on mpv's last
    // playable frame without producing the eof-reached transition that AUTO
    // intentionally uses. Keep normal seeks fast, but use the proven exact EOF
    // path for the final forward hop so manual seek-to-end still advances AUTO.
    if (seconds > 0) {
        const double currentPos = std::max(
            0.0, MpvGetDouble("time-pos", g_timePos));
        if (currentPos + static_cast<double>(seconds) >= g_duration - 0.05) {
            SeekAbsolute(g_duration);
            return;
        }
    }

    // Match mpv's normal Left/Right seek behavior: relative seeks restart at
    // keyframe boundaries, which is deliberately fast when repeated rapidly.
    // osd-msg asks mpv to show its normal seek-time feedback while the client
    // API would otherwise default to no OSD.
    if (MpvCommand({"osd-msg", "seek", std::to_string(seconds),
                    "relative+keyframes"}) && seconds > 0) {
        // Preserve the existing rule that deliberately seeking to the end may
        // advance AUTO but must not by itself kick the player out of fullscreen.
        // PollMpv clears this again as soon as playback is materially before EOF.
        g_suppressNextEofFullscreenExit = true;
    }
}

void PreciseSeekBy(int seconds) {
    if (!g_mpvReady || g_duration <= 0.0) return;
    SeekAbsolute(g_timePos + static_cast<double>(seconds));
}

void SeekAbsolute(double seconds) {
    if (!g_mpvReady || g_duration <= 0.0) return;
    g_keyframeNavIndex = -1;
    seconds = std::clamp(seconds, 0.0, g_duration);

    // A deliberate seek onto the very end may still advance AUTO, but should
    // not be treated as a natural finish for automatic fullscreen exit.
    if (seconds >= g_duration - 0.05) {
        g_suppressNextEofFullscreenExit = true;
    }

    // Seek-bar / absolute STICK KF seeks can still use libmpv's native fast
    // keyframe seek. The Left/Right seek controls use the ffprobe-backed
    // adjacent-keyframe index in SeekBy() so they cannot jump over intermediate
    // keyframes. Frame stepping remains frame-accurate, and EOF remains exact.
    if (g_cutStickKeyframes && seconds < g_duration - 0.05) {
        char amount[64]{};
        sprintf_s(amount, "%.6f", seconds);
        if (MpvCommand({"osd-msg", "seek", amount, "absolute+keyframes"})) {
            g_timePos = seconds;
            g_pollCounter = 3;

            UpdateTimeLabel();
            if (g_seek) {
                InvalidateRect(g_seek, nullptr, FALSE);
            }

            return;
        }
    }

    MpvSetDouble("time-pos", seconds);
    g_timePos = seconds;
    UpdateTimeLabel();
    if (g_seek) InvalidateRect(g_seek, nullptr, FALSE);
    ShowOsdText(FormatTime(g_timePos) + L" / " + FormatTime(g_duration));
}

std::filesystem::path DefaultScreenshotDirectory() {
    PWSTR knownPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_DEFAULT,
                                       nullptr, &knownPath)) && knownPath) {
        std::filesystem::path path(knownPath);
        CoTaskMemFree(knownPath);
        return path / L"WinterStatic Video Cutter";
    }
    if (knownPath) CoTaskMemFree(knownPath);

    wchar_t profile[MAX_PATH * 4]{};
    const DWORD n = GetEnvironmentVariableW(
        L"USERPROFILE", profile, static_cast<DWORD>(std::size(profile)));
    if (n > 0 && n < std::size(profile)) {
        return std::filesystem::path(profile) / L"Pictures" /
               L"WinterStatic Video Cutter";
    }

    return std::filesystem::path(GetExeDirectory()) / L"Screenshots";
}

std::filesystem::path ScreenshotDirectory() {
    if (!g_screenshotDirectory.empty()) {
        return std::filesystem::path(g_screenshotDirectory);
    }
    return DefaultScreenshotDirectory();
}

bool BrowseForScreenshotFolder(HWND owner, std::wstring& selectedPath) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return false;

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }
    dialog->SetTitle(L"Choose screenshot folder");

    const std::filesystem::path initial = selectedPath.empty()
        ? ScreenshotDirectory()
        : std::filesystem::path(selectedPath);
    IShellItem* initialItem = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr,
                                               IID_PPV_ARGS(&initialItem))) &&
        initialItem) {
        dialog->SetFolder(initialItem);
        initialItem->Release();
    }

    hr = dialog->Show(owner);
    if (SUCCEEDED(hr)) {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dialog->GetResult(&result)) && result) {
            PWSTR path = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                selectedPath = path;
                CoTaskMemFree(path);
            }
            result->Release();
        }
    }

    dialog->Release();
    return !selectedPath.empty() && SUCCEEDED(hr);
}

std::filesystem::path NextScreenshotPath(const std::filesystem::path& directory) {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t stem[96]{};
    swprintf_s(stem, L"WinterStatic-%04u-%02u-%02u_%02u-%02u-%02u-%03u",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    std::filesystem::path candidate = directory / (std::wstring(stem) + L".png");
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) return candidate;

    for (unsigned int suffix = 2; suffix < 10000; ++suffix) {
        candidate = directory /
            (std::wstring(stem) + L"-" + std::to_wstring(suffix) + L".png");
        ec.clear();
        if (!std::filesystem::exists(candidate, ec)) return candidate;
    }

    return directory / (std::wstring(stem) + L"-extra.png");
}

void TakeScreenshot() {
    if (!g_mpvReady) return;

    const std::filesystem::path directory = ScreenshotDirectory();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        ShowOsdText(L"Screenshot failed: could not create save folder", 1800);
        return;
    }

    const std::filesystem::path path = NextScreenshotPath(directory);
    if (!MpvCommand({"screenshot-to-file", WideToUtf8(path.wstring()),
                     "subtitles"})) {
        ShowOsdText(L"Screenshot failed", 1500);
        return;
    }

    ShowOsdText(L"Screenshot saved: " + path.filename().wstring(), 1800);
}

void FrameStep(bool next) {
    if (!g_mpvReady || MpvGetFlag("idle-active", true)) return;
    g_keyframeNavIndex = -1;
    MpvCommand({next ? "frame-step" : "frame-back-step"});
    g_pollCounter = 3;
    PollMpv();
}

void ChapterStep(bool next) {
    if (!g_mpvReady) return;
    g_keyframeNavIndex = -1;
    MpvCommand({"add", "chapter", next ? "1" : "-1"});
    g_pollCounter = 3;
    PollMpv();
}

void AdjustSpeed(double delta) {
    if (!g_mpvReady) return;

    double speed = MpvGetDouble("speed", 1.0);
    speed = std::clamp(speed + delta, 0.25, 4.0);

    // Snap to hundredths so repeated +/- presses don't accumulate ugly
    // floating-point tails.
    speed = std::round(speed * 100.0) / 100.0;
    MpvSetDouble("speed", speed);

    wchar_t osd[32]{};
    swprintf_s(osd, L"%.2f\u00D7", speed);
    ShowOsdText(osd, 850);
}

std::wstring DelayOsdText(const wchar_t* label, int milliseconds) {
    wchar_t value[64]{};
    if (milliseconds > 0) {
        swprintf_s(value, L"%ls: +%d ms", label, milliseconds);
    } else {
        swprintf_s(value, L"%ls: %d ms", label, milliseconds);
    }
    return value;
}

void SetSyncDelayMs(const char* property, const wchar_t* label,
                    int milliseconds, bool& touched) {
    if (!g_mpvReady || MpvGetFlag("idle-active", true)) {
        ShowOsdText(L"No media loaded", 1000);
        return;
    }

    if (MpvSetDouble(property, static_cast<double>(milliseconds) / 1000.0)) {
        touched = milliseconds != 0;
        ShowOsdText(DelayOsdText(label, milliseconds), 1000);
    } else {
        ShowOsdText(std::wstring(label) + L" adjustment unavailable", 1400);
    }
}

void AdjustSyncDelayMs(const char* property, const wchar_t* label,
                       int deltaMs, bool& touched) {
    if (!g_mpvReady || MpvGetFlag("idle-active", true)) {
        ShowOsdText(L"No media loaded", 1000);
        return;
    }

    const int currentMs = static_cast<int>(
        std::llround(MpvGetDouble(property, 0.0) * 1000.0));
    SetSyncDelayMs(property, label, currentMs + deltaMs, touched);
}

void SetVolume(int percent) {
    g_volumePercent = std::clamp(percent, 0, 100);
    if (g_mpvReady) MpvSetDouble("volume", static_cast<double>(g_volumePercent));
    if (g_volume) InvalidateRect(g_volume, nullptr, FALSE);
    UpdateVolumeLabel();
    SaveAudioState();
    wchar_t osd[32]{};
    swprintf_s(osd, L"%d%%", g_volumePercent);
    ShowOsdText(osd);
}

void AdjustVolume(int delta) {
    SetVolume(g_volumePercent + delta);
}

int PlaylistOsdFontSize() {
    // Keep the established playlist OSD size. The bounded overlay handles
    // long lists without shrinking the glyphs.
    return std::clamp(static_cast<int>(std::lround(g_osdFontSize * 0.56)), 30, 44);
}

std::wstring PlaylistOverlayDisplayLabel(const PlaylistEntry& entry) {
    std::wstring label;
    if (!entry.title.empty()) {
        label = Utf8ToWide(entry.title);
    } else {
        const std::wstring full = Utf8ToWide(entry.filename);
        if (!full.empty()) {
            const std::filesystem::path path(full);
            label = path.filename().wstring();
            if (label.empty()) label = full;
        }
    }
    if (label.empty()) label = L"Untitled";

    for (wchar_t& ch : label) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    }
    while (label.find(L"  ") != std::wstring::npos) {
        label.replace(label.find(L"  "), 2, L" ");
    }
    return label;
}

size_t PlaylistOverlayMaxLabelChars() {
    // Keep rows single-line. Scale the character budget with the actual video
    // surface width; 96 characters is a conservative 1080p-width target for
    // the proven ~40 OSD size and Segoe UI Semibold.
    RECT r{};
    if (!g_video || !GetClientRect(g_video, &r)) return 72;
    const int width = std::max(1, static_cast<int>(r.right - r.left));
    const double scale = static_cast<double>(width) / 1920.0;
    return static_cast<size_t>(std::clamp(static_cast<int>(std::lround(96.0 * scale)), 48, 104));
}

std::string AssColorFromMpvHex(const std::string& value, const char* fallbackRgb) {
    std::string rgb = value;
    if (rgb.size() == 9 && rgb[0] == '#') rgb = "#" + rgb.substr(3); // #AARRGGBB
    if (rgb.size() != 7 || rgb[0] != '#') rgb = fallbackRgb;
    if (rgb.size() != 7 || rgb[0] != '#') rgb = "#FFFFFF";

    const std::string rr = rgb.substr(1, 2);
    const std::string gg = rgb.substr(3, 2);
    const std::string bb = rgb.substr(5, 2);
    return "&H" + bb + gg + rr + "&";
}

bool RenderPlaylistOverlay(std::wstring* errorText = nullptr) {
    if (!g_mpvReady) return false;

    const auto entries = GetPlaylistEntries();
    if (entries.empty()) return false;

    int64_t pos = MpvGetInt64("playlist-pos", -1);
    if (pos < 0 || pos >= static_cast<int64_t>(entries.size())) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].playing || entries[i].current) {
                pos = static_cast<int64_t>(i);
                break;
            }
        }
    }
    if (pos < 0 || pos >= static_cast<int64_t>(entries.size())) pos = 0;

    constexpr size_t kVisibleEntries = 9;
    const size_t count = entries.size();
    const size_t current = static_cast<size_t>(pos);
    const size_t half = kVisibleEntries / 2;
    size_t start = current > half ? current - half : 0;
    if (start + kVisibleEntries > count) {
        start = count > kVisibleEntries ? count - kVisibleEntries : 0;
    }
    const size_t end = std::min(count, start + kVisibleEntries);
    const size_t maxLabelChars = PlaylistOverlayMaxLabelChars();

    const int fontSize = PlaylistOsdFontSize();
    const int lineHeight = std::max(fontSize + 6, static_cast<int>(std::lround(fontSize * 1.18)));
    const int left = 10;
    int y = 10;

    const std::string normalColor = AssColorFromMpvHex(MpvGetString("osd-color"), "#FFFFFF");
    const std::string selectedColor = AssColorFromMpvHex(MpvGetString("osd-selected-color"), "#FFCC00");

    std::string data;
    auto appendLine = [&](const std::wstring& raw, bool selected) {
        std::wstring line = raw;
        if (line.size() > maxLabelChars) {
            const size_t keep = maxLabelChars > 3 ? maxLabelChars - 3 : maxLabelChars;
            line.resize(keep);
            line += L"...";
        }

        // ass-events splits DATA on real newline characters and turns every
        // line into its own Dialogue event. Give every row its own absolute
        // position rather than relying on embedded \\N line breaks.
        const std::string color = selected ? selectedColor : normalColor;
        data += "{\\an7\\pos(" + std::to_string(left) + "," + std::to_string(y) + ")"
                "\\q2\\fs" + std::to_string(fontSize) + "\\fsp-0.75\\1c" + color + "}";
        data += MpvEscapeAss(WideToUtf8(line));
        data += "\n";
        y += lineHeight;
    };

    wchar_t header[64]{};
    swprintf_s(header, L"Playlist [%lld/%llu]:",
               static_cast<long long>(current + 1),
               static_cast<unsigned long long>(count));
    appendLine(header, false);
    if (start > 0) appendLine(L"...", false);

    for (size_t i = start; i < end; ++i) {
        std::wstring label = PlaylistOverlayDisplayLabel(entries[i]);
        wchar_t prefix[32]{};
        if (i == current) {
            swprintf_s(prefix, L"\u25B6 %llu. ", static_cast<unsigned long long>(i + 1));
        } else {
            swprintf_s(prefix, L"   %llu. ", static_cast<unsigned long long>(i + 1));
        }
        appendLine(std::wstring(prefix) + label, i == current);
    }
    if (end < count) appendLine(L"...", false);

    if (!data.empty() && data.back() == '\n') data.pop_back();

    const int rc = MpvSetTextOverlay("ass-events", data);
    if (rc < 0) {
        if (errorText) *errorText = MpvErrorText(rc);
        return false;
    }
    return true;
}

void ShowPlaylistOsd() {
    if (!g_mpvReady || MpvGetInt64("playlist-count", 0) <= 0) {
        g_playlistOsdVisible = false;
        RemovePlaylistOverlay();
        ShowOsdText(L"No playlist", 1200);
        return;
    }

    if (g_main) KillTimer(g_main, TIMER_PLAYLIST_OSD_RESTORE);
    std::wstring overlayError;
    if (RenderPlaylistOverlay(&overlayError)) {
        g_playlistOsdVisible = true;
    } else {
        g_playlistOsdVisible = false;
        const std::wstring message = overlayError.empty()
            ? L"Playlist overlay could not be rendered"
            : L"Playlist overlay failed: " + overlayError;
        ShowOsdText(message, 3000);
    }
}

void HidePlaylistOsd() {
    if (g_main) KillTimer(g_main, TIMER_PLAYLIST_OSD_RESTORE);
    RemovePlaylistOverlay();
    g_playlistOsdVisible = false;
}

void TogglePlaylistOsd() {
    if (g_playlistOsdVisible) HidePlaylistOsd();
    else ShowPlaylistOsd();
}

void FlashPlaylistOsd(int durationMs) {
    if (!g_mpvReady || MpvGetInt64("playlist-count", 0) <= 0) {
        ShowOsdText(L"No playlist", 1200);
        return;
    }

    if (!RenderPlaylistOverlay()) return;

    if (!g_playlistOsdVisible && g_main) {
        KillTimer(g_main, TIMER_PLAYLIST_OSD_RESTORE);
        SetTimer(g_main, TIMER_PLAYLIST_OSD_RESTORE,
                 static_cast<UINT>(std::max(50, durationMs)), nullptr);
    }
}

void PlaylistStep(bool next) {
    if (!g_mpvReady) return;

    const int64_t count = MpvGetInt64("playlist-count", 0);
    const int64_t pos = MpvGetInt64("playlist-pos", -1);

    // Explicit multi-item playlists keep priority. With one loaded file and
    // AUTO enabled, the same shortcuts navigate the surrounding folder using
    // AUTO's Explorer-style filename ordering.
    if (count <= 1 && g_autoPlayNextFile && !g_currentMediaPath.empty()) {
        const std::wstring adjacent =
            FindAdjacentMediaFileInFolder(g_currentMediaPath, next);
        if (adjacent.empty()) {
            ShowOsdText(next ? L"No next file" : L"No previous file", 1000);
            return;
        }
        LoadFile(adjacent);
        return;
    }

    if (count <= 0 || pos < 0) {
        if (!g_playlistOsdVisible) FlashPlaylistOsd();
        return;
    }

    const int64_t target = next ? pos + 1 : pos - 1;
    if (target < 0 || target >= count) {
        if (!g_playlistOsdVisible) FlashPlaylistOsd();
        return;
    }

    MpvCommand({next ? "playlist-next" : "playlist-prev", "weak"});
}

void PlayPlaylistIndex(size_t index) {
    if (!g_mpvReady) return;

    const int64_t count = MpvGetInt64("playlist-count", 0);
    if (index >= static_cast<size_t>(std::max<int64_t>(0, count))) return;

    const int64_t current = MpvGetInt64("playlist-pos", -1);
    if (current == static_cast<int64_t>(index)) {
        if (!g_playlistOsdVisible) FlashPlaylistOsd();
        return;
    }

    MpvCommand({"playlist-play-index", std::to_string(index)});
}

bool HandlePlaylistMenuChoice(int chosen) {
    if (chosen == IDM_PLAYLIST_SHOW) {
        TogglePlaylistOsd();
        return true;
    }
    if (chosen == IDM_PLAYLIST_PREV) {
        PlaylistStep(false);
        return true;
    }
    if (chosen == IDM_PLAYLIST_NEXT) {
        PlaylistStep(true);
        return true;
    }
    if (chosen >= IDM_CTX_PLAYLIST_BASE &&
        chosen < IDM_CTX_PLAYLIST_BASE + kMaxPlaylistMenuItems) {
        PlayPlaylistIndex(static_cast<size_t>(chosen - IDM_CTX_PLAYLIST_BASE));
        return true;
    }
    return false;
}

void ToggleMute() {
    g_muted = !MpvGetFlag("mute", false);
    if (g_mpvReady) MpvSetFlag("mute", g_muted);
    if (g_mute) InvalidateRect(g_mute, nullptr, FALSE);
    SaveAudioState();
}

void AdjustVolumeFromWheel(WPARAM wParam) {
    g_wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
    while (g_wheelRemainder >= WHEEL_DELTA) {
        AdjustVolume(kVolumeStep);
        g_wheelRemainder -= WHEEL_DELTA;
    }
    while (g_wheelRemainder <= -WHEEL_DELTA) {
        AdjustVolume(-kVolumeStep);
        g_wheelRemainder += WHEEL_DELTA;
    }
}

void ShowTrackMenu(HWND anchor, bool audio) {
    if (!g_mpvReady || !anchor) return;
    const auto tracks = GetTracks(audio ? "audio" : "sub");
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    int command = 1;
    if (!audio) {
        bool anySelected = false;
        for (const auto& t : tracks) anySelected = anySelected || t.selected;
        AppendMenuW(menu, MF_STRING | (anySelected ? 0 : MF_CHECKED), command++, L"Off");
        if (!tracks.empty()) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }

    for (const auto& t : tracks) {
        const std::wstring label = TrackMenuLabel(t);
        AppendMenuW(menu, MF_STRING | (t.selected ? MF_CHECKED : 0), command++, label.c_str());
    }

    if (tracks.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 999, audio ? L"No audio tracks" : L"No subtitle tracks");
    }

    RECT r{};
    GetWindowRect(anchor, &r);
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                                      r.left, r.top, 0, g_main, nullptr);
    if (chosen > 0 && chosen != 999) {
        if (!audio && chosen == 1) {
            if (g_mpvApi.set_property_string(g_mpv, "sid", "no") >= 0) {
                RememberSubtitlesOff();
            }
        } else {
            const int index = chosen - 1 - (audio ? 0 : 1);
            if (index >= 0 && index < static_cast<int>(tracks.size())) {
                const TrackInfo& track = tracks[static_cast<size_t>(index)];
                int64_t id = track.id;
                if (g_mpvApi.set_property(
                        g_mpv,
                        audio ? "aid" : "sid",
                        MPV_FORMAT_INT64,
                        &id) >= 0) {
                    if (audio) RememberAudioTrack(track);
                    else RememberSubtitleTrack(track);
                }
            }
        }
        g_pollCounter = 3;
        PollMpv();
    }
    DestroyMenu(menu);
}

void CycleAudioTrack() {
    if (!g_mpvReady) return;

    const auto tracks = GetTracks("audio");
    if (tracks.empty()) {
        ShowOsdText(L"No audio tracks", 1000);
        return;
    }

    int selected = -1;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].selected) {
            selected = static_cast<int>(i);
            break;
        }
    }

    const size_t next =
        selected < 0
            ? 0
            : (static_cast<size_t>(selected) + 1) % tracks.size();

    int64_t id = tracks[next].id;
    if (g_mpvApi.set_property(g_mpv, "aid", MPV_FORMAT_INT64, &id) >= 0) {
        RememberAudioTrack(tracks[next]);
        g_pollCounter = 3;
        PollMpv();
        ShowOsdText(L"Audio: " + TrackMenuLabel(tracks[next]), 1000);
    }
}

void CycleSubtitleTrack() {
    if (!g_mpvReady) return;

    const auto tracks = GetTracks("sub");
    if (tracks.empty()) {
        ShowOsdText(L"No subtitle tracks", 1000);
        return;
    }

    int selected = -1;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].selected) {
            selected = static_cast<int>(i);
            break;
        }
    }

    // Off -> first subtitle track.
    if (selected < 0) {
        int64_t id = tracks.front().id;
        if (g_mpvApi.set_property(g_mpv, "sid", MPV_FORMAT_INT64, &id) >= 0) {
            RememberSubtitleTrack(tracks.front());
            g_pollCounter = 3;
            PollMpv();
            ShowOsdText(L"Subtitles: " + TrackMenuLabel(tracks.front()), 1000);
        }
        return;
    }

    // Last subtitle track -> Off.
    const size_t next = static_cast<size_t>(selected) + 1;
    if (next >= tracks.size()) {
        if (g_mpvApi.set_property_string(g_mpv, "sid", "no") >= 0) {
            RememberSubtitlesOff();
            g_pollCounter = 3;
            PollMpv();
            ShowOsdText(L"Subtitles: Off", 1000);
        }
        return;
    }

    int64_t id = tracks[next].id;
    if (g_mpvApi.set_property(g_mpv, "sid", MPV_FORMAT_INT64, &id) >= 0) {
        RememberSubtitleTrack(tracks[next]);
        g_pollCounter = 3;
        PollMpv();
        ShowOsdText(L"Subtitles: " + TrackMenuLabel(tracks[next]), 1000);
    }
}

void ToggleMediaInfo() {
    if (!g_mpvReady) {
        MessageBoxW(
            g_main,
            L"Media info is unavailable because libmpv is not initialized.",
            kAppTitle,
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (MpvGetFlag("idle-active", true)) {
        ShowOsdText(L"No media loaded", 1200);
        return;
    }

    if (!IsStatsOverlayAvailable()) {
        g_mediaInfoVisible = false;
        UpdateOsdPosition();
        ShowOsdText(L"Media info is unavailable in this libmpv build", 1800);
        return;
    }

    if (!g_mediaInfoVisible) {
        // The builtin stats overlay uses top-left ASS alignment and, with its
        // default non-persistent mode, mpv's normal OSD path. Apply the safe
        // Media Info margins before toggling it on.
        g_mediaInfoVisible = true;
        UpdateOsdPosition();

        if (!MpvCommand({"script-binding", "stats/display-stats-toggle"})) {
            g_mediaInfoVisible = false;
            UpdateOsdPosition();
            ShowOsdText(L"Media info is unavailable in this libmpv build", 1800);
        }
    } else {
        MpvCommand({"script-binding", "stats/display-stats-toggle"});
        g_mediaInfoVisible = false;

        // Restore the user's normal transient OSD location immediately.
        UpdateOsdPosition();
    }
}

int CutterPanelHeight() {
    int contextRows = 0;
    if (g_cutAudioSyncOpen) ++contextRows;
    if (g_cutSubtitleSyncOpen) ++contextRows;

    return kPanelBaseHeight + contextRows * kCutterContextBarHeight;
}

void ShowCutterWindow(HWND hwnd, bool show) {
    if (hwnd) ShowWindow(hwnd, show ? SW_SHOW : SW_HIDE);
}

void SyncCutterContextVisibility() {
    const bool subtitleSync = g_cutSubtitleSyncOpen;
    const bool audioSync = g_cutAudioSyncOpen;

    // Subtitle setup stays in the SUBS OPT dialog. The expandable context bar
    // is reserved for controls that are useful while watching the video.
    ShowCutterWindow(g_cutSubEnable, false);
    ShowCutterWindow(g_cutSubPathLabel, false);
    ShowCutterWindow(g_cutSubPathEdit, false);
    ShowCutterWindow(g_cutSubBrowse, false);
    ShowCutterWindow(g_cutSubClear, false);
    ShowCutterWindow(g_cutSubSoft, false);
    ShowCutterWindow(g_cutSubBurn, false);
    ShowCutterWindow(g_cutSubTitleLabel, false);
    ShowCutterWindow(g_cutSubTitleEdit, false);
    ShowCutterWindow(g_cutSubKeepEmbedded, false);

    ShowCutterWindow(g_cutSubSyncTrackLabel, subtitleSync);
    ShowCutterWindow(g_cutSubSyncTrack, subtitleSync);
    ShowCutterWindow(g_cutSubOffsetLabel, subtitleSync);
    ShowCutterWindow(g_cutSubOffsetMinus, subtitleSync);
    ShowCutterWindow(g_cutSubOffsetEdit, subtitleSync);
    ShowCutterWindow(g_cutSubOffsetPlus, subtitleSync);
    ShowCutterWindow(g_cutSubOffsetReset, subtitleSync);
    ShowCutterWindow(g_cutSubSyncA, subtitleSync);
    ShowCutterWindow(g_cutSubSyncB, subtitleSync);
    ShowCutterWindow(g_cutSubDrift, subtitleSync);
    ShowCutterWindow(g_cutSubSyncClear, subtitleSync);
    ShowCutterWindow(g_cutSubSyncStatus, subtitleSync);

    ShowCutterWindow(g_cutAudioSyncTrackLabel, audioSync);
    ShowCutterWindow(g_cutAudioSyncTrack, audioSync);
    ShowCutterWindow(g_cutAudioOffsetLabel, audioSync);
    ShowCutterWindow(g_cutAudioOffsetMinus, audioSync);
    ShowCutterWindow(g_cutAudioOffset, audioSync);
    ShowCutterWindow(g_cutAudioOffsetPlus, audioSync);
    ShowCutterWindow(g_cutAudioOffsetReset, audioSync);
    ShowCutterWindow(g_cutAudioSyncStatus, audioSync);
}

void SyncCutterAudioContextControls() {
    RefreshCutterAudioSyncTrackCombo();
    LoadManagedAudioSyncToControls();

    const bool active = CutterHasActiveAudioSyncTarget();
    if (g_cutAudioOffset) EnableWindow(g_cutAudioOffset, active ? TRUE : FALSE);

    if (g_cutAudioSyncStatus) {
        std::wstring status;
        if (const auto* track = ActiveManagedAudioSyncTrackConst()) {
            status = track->embedded
                ? L"Offset applies to this output track and player preview."
                : L"External audio offset applies at conversion; player preview is unchanged.";
        } else {
            status = L"No included audio track selected. Use TRACKS first.";
        }
        SetWindowTextW(g_cutAudioSyncStatus, status.c_str());
    }

    if (g_cutAudioOffsetMinus) InvalidateRect(g_cutAudioOffsetMinus, nullptr, FALSE);
    if (g_cutAudioOffsetPlus) InvalidateRect(g_cutAudioOffsetPlus, nullptr, FALSE);
    if (g_cutAudioOffsetReset) InvalidateRect(g_cutAudioOffsetReset, nullptr, FALSE);
    if (g_cutAudioSyncButton) InvalidateRect(g_cutAudioSyncButton, nullptr, FALSE);
}

void SyncCutterSubtitleContextControls() {

    RefreshCutterSubtitleSyncTrackCombo();

    if (g_cutSubPathEdit) {
        SetWindowTextW(g_cutSubPathEdit, g_cutSubtitlePath.c_str());
    }
    if (g_cutSubTitleEdit) {
        SetWindowTextW(g_cutSubTitleEdit, g_cutSubtitleTitle.c_str());
        EnableWindow(g_cutSubTitleEdit,
                     g_cutSubtitleMode == CutterSubtitleMode::Soft ? TRUE : FALSE);
    }
    if (g_cutSubOffsetEdit) {
        SetWindowTextW(
            g_cutSubOffsetEdit,
            std::to_wstring(g_cutSubtitleOffsetMs).c_str());
        EnableWindow(
            g_cutSubOffsetEdit,
            CutterHasActiveSubtitleSyncTarget() ? TRUE : FALSE);
    }
    // Keep owner-drawn timing buttons as enabled HWNDs even when their
    // actions are not currently available. Disabling this custom window class
    // made the controls paint as blank areas on Windows. ButtonProc applies the
    // logical enabled state instead, so unavailable controls remain visible and
    // dimmed without accepting clicks.
    if (g_cutSubSyncStatus) {
        std::wstring status;
        if (!CutterHasActiveSubtitleSyncTarget()) {
            status = !g_cutSubtitleProbeError.empty()
                ? g_cutSubtitleProbeError
                : L"No sync-capable subtitle selected. Use SUBS OPT first.";
        } else if (g_cutSubtitleSyncASet &&
                   g_cutSubtitleSyncBSet &&
                   !CutterSubtitleDriftReady()) {
            status =
                L"Invalid drift points: subtitle/source time would not move forward.";
        } else {
            status =
                L"A " +
                CutterSyncPointText(
                    g_cutSubtitleSyncASet,
                    g_cutSubtitleSyncAVideoSeconds,
                    g_cutSubtitleSyncAOffsetMs) +
                L"   B " +
                CutterSyncPointText(
                    g_cutSubtitleSyncBSet,
                    g_cutSubtitleSyncBVideoSeconds,
                    g_cutSubtitleSyncBOffsetMs);
        }
        SetWindowTextW(
            g_cutSubSyncStatus,
            status.c_str());
    }

    if (g_cutSubEnable) InvalidateRect(g_cutSubEnable, nullptr, FALSE);
    if (g_cutSubSoft) InvalidateRect(g_cutSubSoft, nullptr, FALSE);
    if (g_cutSubBurn) InvalidateRect(g_cutSubBurn, nullptr, FALSE);
    if (g_cutSubKeepEmbedded) InvalidateRect(g_cutSubKeepEmbedded, nullptr, FALSE);
    if (g_cutSubOffsetMinus) InvalidateRect(g_cutSubOffsetMinus, nullptr, FALSE);
    if (g_cutSubOffsetPlus) InvalidateRect(g_cutSubOffsetPlus, nullptr, FALSE);
    if (g_cutSubOffsetReset) InvalidateRect(g_cutSubOffsetReset, nullptr, FALSE);
    if (g_cutSubSyncA) InvalidateRect(g_cutSubSyncA, nullptr, FALSE);
    if (g_cutSubSyncB) InvalidateRect(g_cutSubSyncB, nullptr, FALSE);
    if (g_cutSubDrift) InvalidateRect(g_cutSubDrift, nullptr, FALSE);
    if (g_cutSubSyncClear) InvalidateRect(g_cutSubSyncClear, nullptr, FALSE);
    if (g_cutSubtitles) InvalidateRect(g_cutSubtitles, nullptr, FALSE);
    if (g_cutSubSyncButton) InvalidateRect(g_cutSubSyncButton, nullptr, FALSE);
}

void SetCutterAudioSyncOpen(bool open) {
    if (g_cutAudioSyncOpen == open) return;

    if (!open) {
        CutterCommitAudioSyncOffsetEdit(false);
        if (g_mpvReady && g_cutAudioPreviewSession) {
            MpvSetDouble("audio-delay", g_cutAudioPreviewBaseDelaySeconds);
        }
        g_cutAudioPreviewSession = false;
    }

    g_cutAudioSyncOpen = open;

    if (open) {
        SyncCutterAudioContextControls();
        // Position hidden controls before revealing the new row.
        if (g_main) LayoutChildren(g_main);
        SyncCutterContextVisibility();
    } else {
        // Hide first, then shrink/reflow the panel.
        SyncCutterContextVisibility();
        if (g_main) LayoutChildren(g_main);
    }

    if (g_cutAudioSyncButton) {
        InvalidateRect(g_cutAudioSyncButton, nullptr, FALSE);
    }
}

void SetCutterSubtitleSyncOpen(bool open) {
    if (g_cutSubtitleSyncOpen == open) return;

    g_cutSubtitleSyncOpen = open;

    if (open) {
        SyncCutterSubtitleContextControls();
        // Position hidden controls before revealing the new row.
        if (g_main) LayoutChildren(g_main);
        SyncCutterContextVisibility();
    } else {
        // Hide first, then shrink/reflow the panel.
        SyncCutterContextVisibility();
        if (g_main) LayoutChildren(g_main);
    }

    if (g_cutSubSyncButton) {
        InvalidateRect(g_cutSubSyncButton, nullptr, FALSE);
    }
}

void LayoutAudioContextBar(int y, int panelWidth) {
    if (!g_cutAudioSyncOpen) return;

    const int h = 24;
    int x = 6;
    auto place = [&](HWND hwnd, int width, int gapAfter = 4) {
        MoveWindow(hwnd, x, y, width, h, TRUE);
        x += width + gapAfter;
    };

    MoveWindow(g_cutAudioSyncTrackLabel, x, y + 3, 30, 18, TRUE);
    x += 32;
    MoveWindow(g_cutAudioSyncTrack, x, y, 270, 180, TRUE);
    x += 276;

    MoveWindow(g_cutAudioOffsetLabel, x, y + 3, 70, 18, TRUE);
    x += 72;
    MoveWindow(g_cutAudioOffset, x, y + 1, 68, 22, TRUE);
    x += 70;
    place(g_cutAudioOffsetMinus, 28, 2);
    place(g_cutAudioOffsetPlus, 28, 6);
    place(g_cutAudioOffsetReset, 54, 12);

    MoveWindow(
        g_cutAudioSyncStatus,
        x,
        y + 3,
        std::max(160, panelWidth - x - 6),
        18,
        TRUE);
}

void LayoutSubtitleContextBar(int y, int panelWidth) {
    if (!g_cutSubtitleSyncOpen) return;

    // Live subtitle timing only. File selection, Soft/Burn, title and embedded
    // track options remain in the SUBS OPT dialog.
    const int h = 24;
    int x = 6;
    auto place = [&](HWND hwnd, int width, int gapAfter = 4) {
        MoveWindow(hwnd, x, y, width, h, TRUE);
        x += width + gapAfter;
    };

    MoveWindow(g_cutSubSyncTrackLabel, x, y + 3, 28, 18, TRUE);
    x += 30;
    MoveWindow(g_cutSubSyncTrack, x, y, 210, 180, TRUE);
    x += 216;

    MoveWindow(g_cutSubOffsetLabel, x, y + 3, 70, 18, TRUE);
    x += 72;
    MoveWindow(g_cutSubOffsetEdit, x, y + 1, 68, 22, TRUE);
    x += 70;
    place(g_cutSubOffsetMinus, 28, 2);
    place(g_cutSubOffsetPlus, 28, 6);
    place(g_cutSubOffsetReset, 54, 12);
    place(g_cutSubSyncA, 64, 4);
    place(g_cutSubSyncB, 64, 8);
    place(g_cutSubDrift, 64, 4);
    place(g_cutSubSyncClear, 72, 12);

    MoveWindow(
        g_cutSubSyncStatus,
        x,
        y + 3,
        std::max(120, panelWidth - x - 6),
        18,
        TRUE);
}

void SetControlsVisible(bool visible) {
    if (!g_fullscreen) visible = true;
    if (g_controlsVisible == visible) return;
    g_controlsVisible = visible;
    ShowWindow(g_panel, visible ? SW_SHOW : SW_HIDE);
    if (visible) {
        SetWindowPos(g_panel, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        g_lastControlsHover = GetTickCount64();
    }
}

void LayoutVideoOverlay() {
    if (!g_video || !g_overlay) return;
    RECT vr{};
    GetClientRect(g_video, &vr);
    const int overlayW = std::max(0, static_cast<int>(vr.right - vr.left));
    const int overlayH = std::max(0, static_cast<int>(vr.bottom - vr.top));
    MoveWindow(g_overlay, 0, 0, overlayW, overlayH, TRUE);
    SetWindowPos(g_overlay, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (g_cropOverlay) {
        MoveWindow(g_cropOverlay, 0, 0, overlayW, overlayH, TRUE);
        SetWindowPos(g_cropOverlay, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void LayoutChildren(HWND hwnd) {
    RECT cr{};
    GetClientRect(hwnd, &cr);
    const int width = std::max(0, static_cast<int>(cr.right - cr.left));
    const int height = std::max(0, static_cast<int>(cr.bottom - cr.top));
    const int panelHeight = CutterPanelHeight();

    if (g_fullscreen) {
        ShowWindow(g_menuBar, SW_HIDE);
        MoveWindow(g_video, 0, 0, width, height, TRUE);
        MoveWindow(g_panel, 0, std::max(0, height - panelHeight), width, panelHeight, TRUE);
        if (g_controlsVisible) SetWindowPos(g_panel, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        ShowWindow(g_menuBar, SW_SHOW);
        MoveWindow(g_menuBar, 0, 0, width, kMenuHeight, TRUE);
        const int videoHeight = std::max(0, height - kMenuHeight - panelHeight);
        MoveWindow(g_video, 0, kMenuHeight, width, videoHeight, TRUE);
        MoveWindow(g_panel, 0, kMenuHeight + videoHeight, width, panelHeight, TRUE);
        SetControlsVisible(true);
    }

    LayoutVideoOverlay();
    if (!g_panel) return;
    RECT pr{};
    GetClientRect(g_panel, &pr);
    const int pw = std::max(0, static_cast<int>(pr.right - pr.left));

    MoveWindow(g_seek, 6, 2, std::max(20, pw - 12), kSeekHeight, TRUE);

    const int y = 25;
    const int h = kControlHeight;
    int x = 6;
    auto placeSquare = [&](HWND w) {
        MoveWindow(w, x, y, 26, h, TRUE);
        x += 30;
    };
    placeSquare(g_play);
    ShowWindow(g_stop, g_showStopButton ? SW_SHOW : SW_HIDE);
    if (g_showStopButton) placeSquare(g_stop);
    placeSquare(g_prev);
    placeSquare(g_seekBack);
    placeSquare(g_seekForward);
    placeSquare(g_next);
    placeSquare(g_speedDown);
    placeSquare(g_speedUp);

    // Cutter build: remove the old AUTO and PLAYLIST chrome. libmpv's playlist
    // remains an internal implementation detail for multi-file input, but these
    // player-centric controls are not part of the cutter workflow.
    ShowWindow(g_autoNext, SW_HIDE);
    ShowWindow(g_playlistOsd, SW_HIDE);
    ShowWindow(g_playlistMenu, SW_HIDE);
    ShowWindow(g_playlistPrevButton, SW_HIDE);
    ShowWindow(g_playlistNextButton, SW_HIDE);

    // LOOP remains useful while inspecting a short region.
    placeSquare(g_loopCurrent);

    int right = pw - 7;

    const int timeW = 112;
    right -= timeW;
    MoveWindow(g_time, right, y + 4, timeW, 18, TRUE);

    right -= 5;
    const int volumeTextW = 42;
    right -= volumeTextW;
    MoveWindow(g_volumeText, right, y + 4, volumeTextW, 18, TRUE);

    right -= 4;
    const int volW = 80;
    right -= volW;
    MoveWindow(g_volume, right, y + 4, volW, 18, TRUE);

    right -= 4;
    right -= 26;
    MoveWindow(g_mute, right, y, 26, h, TRUE);

    // Middle information group:
    //   playback/resolution   [SUB] language   [AUD] language
    // Recompute it whenever track metadata changes so initial short placeholder
    // widths never stay stuck until the next fullscreen/resize cycle.
    const int middleLeft = x + 3;
    const int middleRight = right - 10;
    const int middleAvailable = std::max(0, middleRight - middleLeft);

    auto textWidth = [&](HWND label, const std::wstring& text, int fallback) {
        int width = fallback;
        HDC dc = GetDC(g_panel);
        if (dc) {
            HFONT oldFont = static_cast<HFONT>(SelectObject(dc, g_smallFont));
            SIZE sz{};
            if (!text.empty() &&
                GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz)) {
                width = sz.cx + 6;
            }
            SelectObject(dc, oldFont);
            ReleaseDC(g_panel, dc);
        }
        return width;
    };

    int statusW = textWidth(g_status, g_statusText, 90);
    int audioInfoW = textWidth(g_audioInfo, g_audioInfoText, 56);
    int subInfoW = textWidth(g_subInfo, g_subInfoText, 56);

    constexpr int trackButtonW = 38;
    constexpr int gap = 4;
    const int fixedW = trackButtonW * 2 + gap * 4;

    // At narrow widths the optional Stop and PREV/NEXT controls consume more
    // space on the left. Never let the middle information group run underneath
    // those controls.
    // If even the two track buttons cannot fit, keep
    // only the playback-status field; the same track actions remain available
    // from the menus and keyboard shortcuts.
    if (middleAvailable < fixedW) {
        ShowWindow(g_subs, SW_HIDE);
        ShowWindow(g_subInfo, SW_HIDE);
        ShowWindow(g_audio, SW_HIDE);
        ShowWindow(g_audioInfo, SW_HIDE);
        MoveWindow(g_status, middleLeft, y + 4, middleAvailable, 18, TRUE);
    } else {
        ShowWindow(g_subs, SW_SHOW);
        ShowWindow(g_subInfo, SW_SHOW);
        ShowWindow(g_audio, SW_SHOW);
        ShowWindow(g_audioInfo, SW_SHOW);

        const int textBudget = std::max(0, middleAvailable - fixedW);
        const int desiredText = statusW + audioInfoW + subInfoW;

        if (desiredText > textBudget) {
            // Preserve the two track buttons, then divide the remaining text
            // budget without hard minimums that can cause overlap. Track labels
            // may collapse to a few characters before the status field does.
            const int perTrackCap = textBudget / 4;
            audioInfoW = std::min(audioInfoW, perTrackCap);
            subInfoW = std::min(subInfoW, perTrackCap);
            statusW = std::max(0, textBudget - audioInfoW - subInfoW);
        }

        int middleX = middleLeft;
        MoveWindow(g_status, middleX, y + 4, statusW, 18, TRUE);
        middleX += statusW + gap;

        MoveWindow(g_subs, middleX, y, trackButtonW, h, TRUE);
        middleX += trackButtonW + gap;
        MoveWindow(g_subInfo, middleX, y + 4, subInfoW, 18, TRUE);
        middleX += subInfoW + gap;

        MoveWindow(g_audio, middleX, y, trackButtonW, h, TRUE);
        middleX += trackButtonW + gap;
        MoveWindow(g_audioInfo, middleX, y + 4, audioInfoW, 18, TRUE);
    }

    // Audio and subtitle sync are independent working rows. When both are
    // open, stack them rather than making them compete for one context slot.
    int contextY = 56;
    if (g_cutAudioSyncOpen) {
        LayoutAudioContextBar(contextY, pw);
        contextY += kCutterContextBarHeight;
    }
    if (g_cutSubtitleSyncOpen) {
        LayoutSubtitleContextBar(contextY, pw);
        contextY += kCutterContextBarHeight;
    }

    // Cutter row: keep the workflow in one left-to-right strip. The destructive
    // controls remain together at the end in CLEAR -> NUKE -> CONVERT order.
    const int cutterY = contextY;
    const int cutterH = 24;
    int cutterX = 6;
    auto placeCutter = [&](HWND control, int controlWidth, int gapAfter = 4) {
        MoveWindow(control, cutterX, cutterY, controlWidth, cutterH, TRUE);
        cutterX += controlWidth + gapAfter;
    };

    placeCutter(g_cutOpen, 54, 4);
    placeCutter(g_cutMulti, 62, 4);
    placeCutter(g_cutTracks, 58, 4);
    placeCutter(g_cutAudioSyncButton, 72, 7);
    placeCutter(g_cutMarkAButton, 28, 4);
    placeCutter(g_cutMarkBButton, 28, 7);
    placeCutter(g_cutFramePrev, 36, 4);
    placeCutter(g_cutFrameNext, 36, 7);
    placeCutter(g_cutStickKeyframesButton, 66, 7);
    placeCutter(g_cutCropButton, 54, 4);
    placeCutter(g_cutSubtitles, 68, 4);
    placeCutter(g_cutSubSyncButton, 74, 12);

    MoveWindow(g_cutFormatLabel, cutterX, cutterY + 3, 54, 18, TRUE);
    cutterX += 58;
    MoveWindow(g_cutFormat, cutterX, cutterY, 106, 160, TRUE);
    cutterX += 116;

    MoveWindow(g_cutResLabel, cutterX, cutterY + 3, 28, 18, TRUE);
    cutterX += 30;
    MoveWindow(g_cutResolution, cutterX, cutterY, 84, 140, TRUE);
    cutterX += 94;

    MoveWindow(g_cutAudioLabel, cutterX, cutterY + 3, 28, 18, TRUE);
    cutterX += 30;
    MoveWindow(g_cutAudioModeCombo, cutterX, cutterY, 64, 100, TRUE);
    cutterX += 68;
    MoveWindow(g_cutAudioKbpsEdit, cutterX, cutterY + 1, 44, 22, TRUE);
    cutterX += 50;

    MoveWindow(g_cutSizeLabel, cutterX, cutterY + 3, 50, 18, TRUE);
    cutterX += 52;
    MoveWindow(g_cutSize, cutterX, cutterY + 1, 60, 22, TRUE);
    cutterX += 68;

    placeCutter(g_cutTwoPassButton, 58, 10);

    MoveWindow(g_cutCrfLabel, cutterX, cutterY + 3, 28, 18, TRUE);
    cutterX += 30;
    MoveWindow(g_cutCrfEdit, cutterX, cutterY + 1, 38, 22, TRUE);
    cutterX += 46;

    placeCutter(g_cutOptions, 68, 6);
    placeCutter(g_cutOutput, 60, 6);
    placeCutter(g_cutClear, 54, 6);
    placeCutter(g_cutNuke, 54, 6);
    placeCutter(g_cutConvert, 76, 0);

    // Thin conversion-progress strip along the bottom edge of the panel.
    MoveWindow(g_cutProgress, 6, panelHeight - 12,
               std::max(20, pw - 12), 8, TRUE);

}

void SetWindowedCursorHidden(bool hidden) {
    if (g_windowedCursorHidden == hidden) return;
    g_windowedCursorHidden = hidden;
    SetCursor(hidden ? nullptr : LoadCursorW(nullptr, IDC_ARROW));
}

bool WindowedVideoMouseMovedEnough() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        // Fail visible if Windows cannot provide a pointer position.
        g_haveWindowedVideoMouseAnchor = false;
        return true;
    }

    if (!g_haveWindowedVideoMouseAnchor) {
        g_windowedVideoMouseAnchor = cursor;
        g_haveWindowedVideoMouseAnchor = true;
        return true;
    }

    const LONGLONG dx = static_cast<LONGLONG>(cursor.x) - g_windowedVideoMouseAnchor.x;
    const LONGLONG dy = static_cast<LONGLONG>(cursor.y) - g_windowedVideoMouseAnchor.y;
    const LONGLONG threshold = kWindowedCursorWakeDistancePx;
    if (dx * dx + dy * dy < threshold * threshold) return false;

    g_windowedVideoMouseAnchor = cursor;
    return true;
}

void NoteWindowedVideoMouseActivity() {
    if (g_fullscreen) return;

    const bool wasOverVideo = g_cursorOverVideo;
    g_cursorOverVideo = true;

    // Entering the video counts as activity. While already over it, ignore
    // repeated same-position WM_MOUSEMOVE messages and tiny pointer jitter.
    if (!wasOverVideo) g_haveWindowedVideoMouseAnchor = false;
    if (!WindowedVideoMouseMovedEnough()) return;

    g_lastWindowedVideoMouseActivity = GetTickCount64();
    SetWindowedCursorHidden(false);
}

void CheckWindowedCursorHide() {
    if (g_fullscreen || !g_hideWindowedCursor || !g_cursorOverVideo) {
        SetWindowedCursorHidden(false);
        return;
    }
    if (!g_mpvReady || !g_playing ||
        GetForegroundWindow() != g_main ||
        GetCapture() != nullptr) {
        SetWindowedCursorHidden(false);
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (now - g_lastWindowedVideoMouseActivity >= kFullscreenCursorHideMs) {
        SetWindowedCursorHidden(true);
    }
}

void SetFullscreenCursorHidden(bool hidden) {
    if (g_fullscreenCursorHidden == hidden) return;
    g_fullscreenCursorHidden = hidden;
    SetCursor(hidden ? nullptr : LoadCursorW(nullptr, IDC_ARROW));
}

void NoteFullscreenMouseActivity() {
    if (!g_fullscreen) return;
    g_lastFullscreenMouseActivity = GetTickCount64();
    SetFullscreenCursorHidden(false);
}

class ScopedWindowRedrawSuppression {
public:
    explicit ScopedWindowRedrawSuppression(HWND hwnd) : hwnd_(hwnd) {
        if (hwnd_) SendMessageW(hwnd_, WM_SETREDRAW, FALSE, 0);
    }

    ~ScopedWindowRedrawSuppression() {
        if (!hwnd_) return;
        SendMessageW(hwnd_, WM_SETREDRAW, TRUE, 0);

        // Invalidate the completed hierarchy normally so Windows can coalesce
        // child paints with the rest of the message loop. Then request one
        // deferred top-level finish on the next message-loop turn instead of
        // forcing paint synchronously inside the fullscreen transition.
        RedrawWindow(hwnd_, nullptr, nullptr,
                     RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);

        if (!g_finalFullscreenRedrawPending &&
            PostMessageW(hwnd_, WM_APP_FINAL_FULLSCREEN_REDRAW, 0, 0)) {
            g_finalFullscreenRedrawPending = true;
        }
    }

    ScopedWindowRedrawSuppression(const ScopedWindowRedrawSuppression&) = delete;
    ScopedWindowRedrawSuppression& operator=(const ScopedWindowRedrawSuppression&) = delete;

private:
    HWND hwnd_ = nullptr;
};

class ScopedFullscreenSizeLayoutSuppression {
public:
    ScopedFullscreenSizeLayoutSuppression()
        : previous_(g_suppressFullscreenSizeLayout) {
        g_suppressFullscreenSizeLayout = true;
    }

    ~ScopedFullscreenSizeLayoutSuppression() {
        g_suppressFullscreenSizeLayout = previous_;
    }

    ScopedFullscreenSizeLayoutSuppression(const ScopedFullscreenSizeLayoutSuppression&) = delete;
    ScopedFullscreenSizeLayoutSuppression& operator=(const ScopedFullscreenSizeLayoutSuppression&) = delete;

private:
    bool previous_ = false;
};

void RefreshFullscreenMonitorBounds(HWND hwnd) {
    if (!g_fullscreen || !hwnd) return;

    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (!monitor || !GetMonitorInfoW(monitor, &mi)) return;

    RECT current{};
    if (GetWindowRect(hwnd, &current) && EqualRect(&current, &mi.rcMonitor)) {
        return;
    }

    ScopedWindowRedrawSuppression redraw(hwnd);
    ScopedFullscreenSizeLayoutSuppression sizeLayout;

    if (!SetWindowPos(hwnd, nullptr,
                      mi.rcMonitor.left, mi.rcMonitor.top,
                      mi.rcMonitor.right - mi.rcMonitor.left,
                      mi.rcMonitor.bottom - mi.rcMonitor.top,
                      SWP_NOACTIVATE | SWP_NOCOPYBITS |
                      SWP_NOZORDER | SWP_NOOWNERZORDER)) {
        return;
    }

    LayoutChildren(hwnd);
    if (g_playlistOsdVisible) RenderPlaylistOverlay();
}

void EnterFullscreen() {
    if (g_fullscreen) return;

    // Preflight all state needed for a safe round-trip before changing either
    // the logical fullscreen state or the visible window hierarchy.
    const DWORD windowedStyle = static_cast<DWORD>(GetWindowLongPtrW(g_main, GWL_STYLE));
    const DWORD windowedExStyle = static_cast<DWORD>(GetWindowLongPtrW(g_main, GWL_EXSTYLE));
    RECT windowedRect{};
    WINDOWPLACEMENT windowedPlacement{ sizeof(WINDOWPLACEMENT) };
    MONITORINFO mi{ sizeof(mi) };
    const HMONITOR monitor = MonitorFromWindow(g_main, MONITOR_DEFAULTTONEAREST);

    if (!GetWindowRect(g_main, &windowedRect) ||
        !GetWindowPlacement(g_main, &windowedPlacement) ||
        !monitor || !GetMonitorInfoW(monitor, &mi)) {
        return;
    }

    g_windowedStyle = windowedStyle;
    g_windowedExStyle = windowedExStyle;
    g_windowedRect = windowedRect;
    g_windowedPlacement = windowedPlacement;

    g_cursorOverVideo = false;
    g_trackingVideoMouseLeave = false;
    g_haveWindowedVideoMouseAnchor = false;
    SetWindowedCursorHidden(false);

    {
        // The scope guard guarantees WM_SETREDRAW is restored and the final
        // frame is invalidated even if this transition later gains an early exit.
        ScopedWindowRedrawSuppression redraw(g_main);
        ScopedFullscreenSizeLayoutSuppression sizeLayout;

        g_fullscreen = true;
        g_controlsVisible = false;

        SetWindowLongPtrW(g_main, GWL_STYLE,
                          (g_windowedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
        SetWindowLongPtrW(g_main, GWL_EXSTYLE, g_windowedExStyle & ~WS_EX_CLIENTEDGE);
        SetWindowPos(g_main, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW |
                     SWP_NOACTIVATE | SWP_NOCOPYBITS);

        ShowWindow(g_panel, SW_HIDE);
        LayoutChildren(g_main);
        if (g_playlistOsdVisible) RenderPlaylistOverlay();
    }

    g_lastFullscreenMouseActivity = GetTickCount64();
    SetFullscreenCursorHidden(false);
    SetTimer(g_main, TIMER_FULLSCREEN_HOVER, 100, nullptr);
}

void ExitFullscreen() {
    if (!g_fullscreen) return;
    KillTimer(g_main, TIMER_FULLSCREEN_HOVER);
    SetFullscreenCursorHidden(false);

    {
        // Keep the logical fullscreen layout active until the saved top-level
        // state is restored. The guard cannot leave redraw disabled on scope exit.
        ScopedWindowRedrawSuppression redraw(g_main);
        ScopedFullscreenSizeLayoutSuppression sizeLayout;

        SetWindowLongPtrW(g_main, GWL_STYLE, g_windowedStyle);
        SetWindowLongPtrW(g_main, GWL_EXSTYLE, g_windowedExStyle);

        bool placementRestored = false;
        if (g_windowedPlacement.length == sizeof(WINDOWPLACEMENT)) {
            placementRestored = SetWindowPlacement(g_main, &g_windowedPlacement) != FALSE;
            if (placementRestored) {
                SetWindowPos(g_main, nullptr, 0, 0, 0, 0,
                             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                             SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
            }
        }

        if (!placementRestored) {
            SetWindowPos(g_main, nullptr,
                         g_windowedRect.left, g_windowedRect.top,
                         g_windowedRect.right - g_windowedRect.left,
                         g_windowedRect.bottom - g_windowedRect.top,
                         SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        }

        g_fullscreen = false;
        LayoutChildren(g_main);
        if (g_playlistOsdVisible) RenderPlaylistOverlay();
    }

    g_haveWindowedVideoMouseAnchor = false;
    g_lastWindowedVideoMouseActivity = GetTickCount64();
    SetWindowedCursorHidden(false);
}

void ToggleFullscreen() {
    if (g_fullscreen) ExitFullscreen(); else EnterFullscreen();
}

void ActivateMainWindowFromTaskbar() {
    if (!g_main) return;

    if (IsIconic(g_main)) ShowWindow(g_main, SW_RESTORE);
    else if (!IsWindowVisible(g_main)) ShowWindow(g_main, SW_SHOW);

    // THBF_DISMISSONCLICK closes the Shell thumbnail only after the click
    // notification has returned.  Activation therefore happens on a short
    // one-shot timer so the taskbar flyout cannot immediately take focus back.
    BringWindowToTop(g_main);
    SetWindowPos(g_main, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(g_main);
    SetActiveWindow(g_main);
}

void ToggleFullscreenFromTaskbar() {
    if (!g_main) return;

    // A thumbbar click can arrive while the real top-level window is minimized.
    // Restore it before EnterFullscreen() snapshots the windowed placement;
    // otherwise the saved placement can remain a minimized shell-preview state
    // and restoring from fullscreen may leave the video child in a bad layout.
    if (!g_fullscreen) {
        if (IsIconic(g_main)) ShowWindow(g_main, SW_RESTORE);
        else if (!IsWindowVisible(g_main)) ShowWindow(g_main, SW_SHOW);
        UpdateWindow(g_main);
    }

    ToggleFullscreen();

    // The Shell dismisses its thumbnail flyout after THBN_CLICKED returns.
    // Re-assert foreground activation just after that dismissal so clicking
    // Fullscreen behaves like a direct player command: the player itself comes
    // forward immediately instead of remaining behind the taskbar preview.
    KillTimer(g_main, TIMER_TASKBAR_ACTIVATE);
    SetTimer(g_main, TIMER_TASKBAR_ACTIVATE, 75, nullptr);
}

void CheckFullscreenControlsHover() {
    if (!g_fullscreen) return;
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(g_main, &cursor);
    RECT client{};
    GetClientRect(g_main, &client);
    const ULONGLONG now = GetTickCount64();
    const bool inClient = PtInRect(&client, cursor) != FALSE;
    const int panelHeight = CutterPanelHeight();
    const bool overControls =
        inClient && g_controlsVisible &&
        cursor.y >= client.bottom - panelHeight && cursor.y <= client.bottom + 2;

    // Never let a fullscreen player on one monitor hide the pointer while it
    // is actually parked on another monitor/window.
    if (!inClient) SetFullscreenCursorHidden(false);

    // Keep the pointer available while the control panel is being used. Over
    // the video itself, hide it after a short idle period like normal players.
    if (overControls) {
        g_lastControlsHover = now;
        SetFullscreenCursorHidden(false);
    } else if (inClient && GetCapture() == nullptr &&
               now - g_lastFullscreenMouseActivity >= kFullscreenCursorHideMs) {
        SetFullscreenCursorHidden(true);
    }

    if (!g_controlsVisible) {
        if (inClient && cursor.y >= client.bottom - kRevealEdge && cursor.y <= client.bottom + 2) {
            SetControlsVisible(true);
        }
        return;
    }
    if (overControls) return;
    if (now - g_lastControlsHover > 1100 && GetCapture() == nullptr) SetControlsVisible(false);
}

RECT MenuItemRect(int index) {
    static constexpr int widths[] = { 42, 44, 42, 44 };
    int x = 6;
    for (int i = 0; i < index; ++i) x += widths[i];
    return RECT{ x, 0, x + widths[index], kMenuHeight };
}

int HitTestMenu(int x, int y) {
    if (y < 0 || y >= kMenuHeight) return -1;
    for (int i = 0; i < 4; ++i) {
        RECT r = MenuItemRect(i);
        if (x >= r.left && x < r.right) return i;
    }
    return -1;
}

LRESULT CALLBACK MenuMessageFilterProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == MSGF_MENU && g_menuBar && g_activeTopMenu >= 0) {
        MSG* message = reinterpret_cast<MSG*>(lParam);
        if (message && (message->message == WM_MOUSEMOVE ||
                        message->message == WM_LBUTTONDOWN ||
                        message->message == WM_LBUTTONUP)) {
            POINT p{};
            GetCursorPos(&p);
            ScreenToClient(g_menuBar, &p);
            const int hit = HitTestMenu(p.x, p.y);
            if (hit >= 0 && hit != g_activeTopMenu) {
                g_menuSwitchRequest = hit;
                EndMenu();
                return 1;
            }
        }
    }
    return CallNextHookEx(g_menuHook, code, wParam, lParam);
}

HMENU BuildContextTrackSubmenu(bool audio, const std::vector<TrackInfo>& tracks) {
    HMENU submenu = CreatePopupMenu();
    if (!submenu) return nullptr;

    if (!audio) {
        bool anySelected = false;
        for (const auto& t : tracks) {
            if (t.selected) {
                anySelected = true;
                break;
            }
        }

        AppendMenuW(submenu,
                    MF_STRING | (anySelected ? 0 : MF_CHECKED),
                    IDM_CTX_SUB_TRACK_OFF,
                    L"Off");

        if (!tracks.empty()) {
            AppendMenuW(submenu, MF_SEPARATOR, 0, nullptr);
        }
    }

    if (tracks.empty()) {
        AppendMenuW(submenu, MF_STRING | MF_GRAYED, 0,
                    audio ? L"No audio tracks" : L"No subtitle tracks");
        return submenu;
    }

    const int base = audio ? IDM_CTX_AUDIO_TRACK_BASE : IDM_CTX_SUB_TRACK_BASE;
    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& track = tracks[i];
        const std::wstring label = TrackMenuLabel(track);
        AppendMenuW(submenu,
                    MF_STRING | (track.selected ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(base + static_cast<int>(i)),
                    label.c_str());
    }

    return submenu;
}

HMENU BuildPlaylistSubmenu() {
    HMENU submenu = CreatePopupMenu();
    if (!submenu) return nullptr;

    const std::wstring showLabel =
        ShortcutMenuLabel(L"Show / hide playlist", ShortcutAction::TogglePlaylist);
    const std::wstring prevLabel =
        ShortcutMenuLabel(L"Previous item", ShortcutAction::PreviousPlaylist);
    const std::wstring nextLabel =
        ShortcutMenuLabel(L"Next item", ShortcutAction::NextPlaylist);

    AppendMenuW(submenu, MF_STRING | (g_playlistOsdVisible ? MF_CHECKED : 0),
                IDM_PLAYLIST_SHOW, showLabel.c_str());
    AppendMenuW(submenu, MF_STRING, IDM_PLAYLIST_PREV, prevLabel.c_str());
    AppendMenuW(submenu, MF_STRING, IDM_PLAYLIST_NEXT, nextLabel.c_str());
    AppendMenuW(submenu, MF_SEPARATOR, 0, nullptr);

    const auto entries = GetPlaylistEntries();
    if (entries.empty()) {
        AppendMenuW(submenu, MF_STRING | MF_GRAYED, 0, L"No playlist items");
        return submenu;
    }

    const bool anyPlaying = std::any_of(
        entries.begin(), entries.end(),
        [](const PlaylistEntry& entry) { return entry.playing; });

    const size_t visibleCount =
        std::min(entries.size(), static_cast<size_t>(kMaxPlaylistMenuItems));
    for (size_t i = 0; i < visibleCount; ++i) {
        const auto& entry = entries[i];
        const std::wstring label = PlaylistEntryLabel(entry, i);
        const bool active = entry.playing || (!anyPlaying && entry.current);
        AppendMenuW(submenu,
                    MF_STRING | (active ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(IDM_CTX_PLAYLIST_BASE + static_cast<int>(i)),
                    label.c_str());
    }

    if (entries.size() > visibleCount) {
        AppendMenuW(submenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(submenu, MF_STRING | MF_GRAYED, 0, L"Playlist too large to show completely");
    }

    return submenu;
}

void ShowPlaylistPopup(HWND anchor) {
    HMENU menu = BuildPlaylistSubmenu();
    if (!menu) return;

    RECT r{};
    GetWindowRect(anchor ? anchor : g_main, &r);
    const int chosen = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
        r.left, r.top, 0, g_main, nullptr);

    if (chosen) HandlePlaylistMenuChoice(chosen);
    DestroyMenu(menu);
}

void BuildMenus() {
    g_fileMenu = CreatePopupMenu();
    g_viewMenu = CreatePopupMenu();
    g_playMenu = CreatePopupMenu();
    g_helpMenu = CreatePopupMenu();

    const std::wstring openLabel =
        ShortcutMenuLabel(L"&Open...", ShortcutAction::OpenFile);
    const std::wstring screenshotLabel =
        ShortcutMenuLabel(L"Take &screenshot", ShortcutAction::TakeScreenshot);
    AppendMenuW(g_fileMenu, MF_STRING, IDM_FILE_OPEN, openLabel.c_str());
    AppendMenuW(g_fileMenu, MF_STRING, IDM_FILE_SCREENSHOT, screenshotLabel.c_str());
    AppendMenuW(g_fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_fileMenu, MF_STRING, IDM_FILE_EXIT, L"E&xit");

    const std::wstring fullscreenLabel =
        ShortcutMenuLabel(L"&Fullscreen", ShortcutAction::ToggleFullscreen);
    const std::wstring mediaInfoLabel =
        ShortcutMenuLabel(L"&Media info", ShortcutAction::MediaInfo);
    AppendMenuW(g_viewMenu, MF_STRING, IDM_VIEW_FULLSCREEN, fullscreenLabel.c_str());
    AppendMenuW(g_viewMenu, MF_STRING, IDM_VIEW_MEDIA_INFO, mediaInfoLabel.c_str());
    AppendMenuW(g_viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_viewMenu, MF_STRING, IDM_VIEW_OPTIONS, L"&Player options...");
    AppendMenuW(g_viewMenu, MF_STRING, IDM_VIEW_CUTTER_OPTIONS, L"&Cutter options...");

    HMENU mpvConfigMenu = CreatePopupMenu();
    AppendMenuW(mpvConfigMenu, MF_STRING,
                IDM_VIEW_OPEN_MPV_CONFIG, L"&Open mpv.conf");
    AppendMenuW(g_viewMenu, MF_POPUP,
                reinterpret_cast<UINT_PTR>(mpvConfigMenu),
                L"mpv &configuration");

    const std::wstring playLabel =
        ShortcutMenuLabel(L"&Play / Pause", ShortcutAction::PlayPause);
    const std::wstring stopLabel =
        ShortcutMenuLabel(L"&Stop", ShortcutAction::Stop);
    const std::wstring prevChapterLabel =
        ShortcutMenuLabel(L"Previous chapter", ShortcutAction::PreviousChapter);
    const std::wstring seekBackLabel =
        ShortcutMenuLabel(L"Seek back 5 seconds", ShortcutAction::SeekBackward);
    const std::wstring seekForwardLabel =
        ShortcutMenuLabel(L"Seek forward 5 seconds", ShortcutAction::SeekForward);
    const std::wstring nextChapterLabel =
        ShortcutMenuLabel(L"Next chapter", ShortcutAction::NextChapter);
    const std::wstring speedDownLabel =
        ShortcutMenuLabel(L"Decrease speed", ShortcutAction::SpeedDown);
    const std::wstring speedUpLabel =
        ShortcutMenuLabel(L"Increase speed", ShortcutAction::SpeedUp);
    const std::wstring muteLabel =
        ShortcutMenuLabel(L"Mute", ShortcutAction::Mute);
    const std::wstring loopLabel =
        ShortcutMenuLabel(L"Loop current file", ShortcutAction::ToggleLoopCurrent);

    AppendMenuW(g_playMenu, MF_STRING, IDM_PLAY_TOGGLE, playLabel.c_str());
    AppendMenuW(g_playMenu, MF_STRING, IDM_PLAY_STOP, stopLabel.c_str());
    AppendMenuW(g_playMenu, MF_STRING, IDM_LOOP_CURRENT_FILE, loopLabel.c_str());
    AppendMenuW(g_playMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_playMenu, MF_STRING, IDM_PLAY_PREV, prevChapterLabel.c_str());
    AppendMenuW(g_playMenu, MF_STRING, IDM_PLAY_BACK, seekBackLabel.c_str());
    AppendMenuW(g_playMenu, MF_STRING, IDM_PLAY_FORWARD, seekForwardLabel.c_str());
    AppendMenuW(g_playMenu, MF_STRING, IDM_PLAY_NEXT, nextChapterLabel.c_str());
    AppendMenuW(g_playMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_playMenu, MF_STRING, IDM_SPEED_DOWN, speedDownLabel.c_str());
    AppendMenuW(g_playMenu, MF_STRING, IDM_SPEED_UP, speedUpLabel.c_str());
    AppendMenuW(g_playMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_playMenu, MF_STRING, IDM_AUDIO_TRACKS, L"Audio tracks...");
    AppendMenuW(g_playMenu, MF_STRING, IDM_SUB_TRACKS, L"Subtitle tracks...");
    AppendMenuW(g_playMenu, MF_STRING, IDM_MUTE, muteLabel.c_str());

    AppendMenuW(g_helpMenu, MF_STRING, IDM_HELP_ABOUT, L"&About");
}

void RefreshMenuShortcutLabels() {
    if (g_fileMenu) {
        const std::wstring openLabel =
            ShortcutMenuLabel(L"&Open...", ShortcutAction::OpenFile);
        const std::wstring screenshotLabel =
            ShortcutMenuLabel(L"Take &screenshot", ShortcutAction::TakeScreenshot);
        ModifyMenuW(g_fileMenu, IDM_FILE_OPEN,
                    MF_BYCOMMAND | MF_STRING, IDM_FILE_OPEN, openLabel.c_str());
        ModifyMenuW(g_fileMenu, IDM_FILE_SCREENSHOT,
                    MF_BYCOMMAND | MF_STRING, IDM_FILE_SCREENSHOT,
                    screenshotLabel.c_str());
    }

    if (g_viewMenu) {
        const std::wstring fullscreen =
            ShortcutMenuLabel(L"&Fullscreen", ShortcutAction::ToggleFullscreen);
        const std::wstring mediaInfo =
            ShortcutMenuLabel(L"&Media info", ShortcutAction::MediaInfo);
        ModifyMenuW(g_viewMenu, IDM_VIEW_FULLSCREEN,
                    MF_BYCOMMAND | MF_STRING, IDM_VIEW_FULLSCREEN,
                    fullscreen.c_str());
        ModifyMenuW(g_viewMenu, IDM_VIEW_MEDIA_INFO,
                    MF_BYCOMMAND | MF_STRING, IDM_VIEW_MEDIA_INFO,
                    mediaInfo.c_str());
    }

    if (g_playMenu) {
        struct MenuShortcut {
            UINT id;
            const wchar_t* label;
            ShortcutAction action;
        };

        const MenuShortcut items[] = {
            { IDM_PLAY_TOGGLE, L"&Play / Pause", ShortcutAction::PlayPause },
            { IDM_PLAY_STOP, L"&Stop", ShortcutAction::Stop },
            { IDM_PLAY_PREV, L"Previous chapter", ShortcutAction::PreviousChapter },
            { IDM_PLAY_BACK, L"Seek back 5 seconds", ShortcutAction::SeekBackward },
            { IDM_PLAY_FORWARD, L"Seek forward 5 seconds", ShortcutAction::SeekForward },
            { IDM_PLAY_NEXT, L"Next chapter", ShortcutAction::NextChapter },
            { IDM_SPEED_DOWN, L"Decrease speed", ShortcutAction::SpeedDown },
            { IDM_SPEED_UP, L"Increase speed", ShortcutAction::SpeedUp },
            { IDM_MUTE, L"Mute", ShortcutAction::Mute },
            { IDM_LOOP_CURRENT_FILE, L"Loop current file", ShortcutAction::ToggleLoopCurrent }
        };

        for (const auto& item : items) {
            const std::wstring label = ShortcutMenuLabel(item.label, item.action);
            UINT flags = MF_BYCOMMAND | MF_STRING;
            if (item.id == IDM_LOOP_CURRENT_FILE && g_loopCurrentFile) {
                flags |= MF_CHECKED;
            }
            ModifyMenuW(g_playMenu, item.id, flags, item.id, label.c_str());
        }
    }
}

void ShowTopMenu(int index) {
    int current = index;

    for (;;) {
        HMENU popup = nullptr;
        switch (current) {
        case 0: popup = g_fileMenu; break;
        case 1: popup = g_viewMenu; break;
        case 2: popup = g_playMenu; break;
        case 3: popup = g_helpMenu; break;
        default: return;
        }

        g_activeTopMenu = current;
        g_menuSwitchRequest = -1;
        g_menuHover = current;
        InvalidateRect(g_menuBar, nullptr, FALSE);

        RECT item = MenuItemRect(current);
        POINT p{ item.left, item.bottom };
        ClientToScreen(g_menuBar, &p);

        g_menuHook = SetWindowsHookExW(
            WH_MSGFILTER, MenuMessageFilterProc, nullptr, GetCurrentThreadId());

        const int chosen = TrackPopupMenu(
            popup, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
            p.x, p.y, 0, g_main, nullptr);

        if (g_menuHook) {
            UnhookWindowsHookEx(g_menuHook);
            g_menuHook = nullptr;
        }

        if (g_menuSwitchRequest >= 0 && g_menuSwitchRequest != current) {
            current = g_menuSwitchRequest;
            continue;
        }

        g_activeTopMenu = -1;
        if (chosen) SendMessageW(g_main, WM_COMMAND, static_cast<WPARAM>(chosen), 0);
        break;
    }

    g_activeTopMenu = -1;
    g_menuHover = -1;
    InvalidateRect(g_menuBar, nullptr, FALSE);
}

void ShowContextMenu(POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    // Fetch each type once for this popup. The toolbar AUD/SUB buttons continue
    // to use ShowTrackMenu(); only the right-click path becomes cascading.
    const auto audioTracks = GetTracks("audio");
    const auto subtitleTracks = GetTracks("sub");

    HMENU audioMenu = BuildContextTrackSubmenu(true, audioTracks);
    HMENU subtitleMenu = BuildContextTrackSubmenu(false, subtitleTracks);
    HMENU playlistMenu = BuildPlaylistSubmenu();

    AppendMenuW(menu, MF_STRING, IDM_FILE_OPEN, L"Open...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_PLAY_TOGGLE, L"Play / Pause");
    AppendMenuW(menu, MF_STRING, IDM_PLAY_STOP, L"Stop");
    AppendMenuW(menu,
                MF_STRING | (g_loopCurrentFile ? MF_CHECKED : 0),
                IDM_LOOP_CURRENT_FILE,
                L"Loop current file");
    AppendMenuW(menu,
                MF_STRING | (g_autoPlayNextFile ? MF_CHECKED : 0),
                IDM_AUTO_NEXT_FILE,
                L"Auto-play next file in folder");
    if (playlistMenu) {
        AppendMenuW(menu, MF_POPUP,
                    reinterpret_cast<UINT_PTR>(playlistMenu),
                    L"Playlist");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    if (audioMenu) {
        AppendMenuW(menu, MF_POPUP,
                    reinterpret_cast<UINT_PTR>(audioMenu),
                    L"Audio tracks");
    }
    if (subtitleMenu) {
        AppendMenuW(menu, MF_POPUP,
                    reinterpret_cast<UINT_PTR>(subtitleMenu),
                    L"Subtitle tracks");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_VIEW_FULLSCREEN, L"Fullscreen");
    AppendMenuW(menu, MF_STRING, IDM_VIEW_MEDIA_INFO, L"Media info");
    AppendMenuW(menu, MF_STRING, IDM_VIEW_OPTIONS, L"Player options...");
    AppendMenuW(menu, MF_STRING, IDM_VIEW_CUTTER_OPTIONS, L"Cutter options...");

    const int chosen = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x, screenPoint.y, 0, g_main, nullptr);

    if (HandlePlaylistMenuChoice(chosen)) {
        // Handled by the shared playlist menu path.
    } else if (chosen == IDM_CTX_SUB_TRACK_OFF) {
        if (g_mpvReady &&
            g_mpvApi.set_property_string(g_mpv, "sid", "no") >= 0) {
            RememberSubtitlesOff();
            g_pollCounter = 3;
            PollMpv();
        }
    } else if (chosen >= IDM_CTX_AUDIO_TRACK_BASE &&
               chosen < IDM_CTX_AUDIO_TRACK_BASE + static_cast<int>(audioTracks.size())) {
        const size_t index = static_cast<size_t>(chosen - IDM_CTX_AUDIO_TRACK_BASE);
        int64_t id = audioTracks[index].id;
        if (g_mpvReady &&
            g_mpvApi.set_property(g_mpv, "aid", MPV_FORMAT_INT64, &id) >= 0) {
            RememberAudioTrack(audioTracks[index]);
            g_pollCounter = 3;
            PollMpv();
        }
    } else if (chosen >= IDM_CTX_SUB_TRACK_BASE &&
               chosen < IDM_CTX_SUB_TRACK_BASE + static_cast<int>(subtitleTracks.size())) {
        const size_t index = static_cast<size_t>(chosen - IDM_CTX_SUB_TRACK_BASE);
        int64_t id = subtitleTracks[index].id;
        if (g_mpvReady &&
            g_mpvApi.set_property(g_mpv, "sid", MPV_FORMAT_INT64, &id) >= 0) {
            RememberSubtitleTrack(subtitleTracks[index]);
            g_pollCounter = 3;
            PollMpv();
        }
    } else if (chosen) {
        SendMessageW(g_main, WM_COMMAND, static_cast<WPARAM>(chosen), 0);
    }

    // Destroying the parent menu also destroys its attached submenus.
    DestroyMenu(menu);
}

void DrawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void DrawTriangle(HDC dc, POINT a, POINT b, POINT c, COLORREF color) {
    POINT pts[] = { a, b, c };
    HBRUSH br = CreateSolidBrush(color);
    HBRUSH old = static_cast<HBRUSH>(SelectObject(dc, br));
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    Polygon(dc, pts, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, old);
    DeleteObject(pen);
    DeleteObject(br);
}

bool CutterButtonLogicallyEnabled(int id) {
    switch (id) {
    case IDC_CUT_AUDIO_OFFSET_MINUS:
    case IDC_CUT_AUDIO_OFFSET_PLUS:
    case IDC_AUDIO_SYNC_OFFSET_RESET:
        return CutterHasActiveAudioSyncTarget();
    case IDC_SUB_OFFSET_MINUS:
    case IDC_SUB_OFFSET_PLUS:
    case IDC_SUB_OFFSET_RESET:
    case IDC_SUB_SYNC_A:
    case IDC_SUB_SYNC_B:
        return CutterHasActiveSubtitleSyncTarget();
    case IDC_SUB_DRIFT:
        return CutterHasActiveSubtitleSyncTarget() &&
               CutterSubtitleDriftReady();
    case IDC_SUB_SYNC_CLEAR:
        return CutterHasActiveSubtitleSyncTarget() &&
               (g_cutSubtitleSyncASet || g_cutSubtitleSyncBSet);
    default:
        return true;
    }
}

void DrawButtonGlyph(HWND hwnd, HDC dc, int id, const RECT& r, bool logicalEnabled = true) {
    const int cx = (r.left + r.right) / 2;
    const int cy = (r.top + r.bottom) / 2;
    const COLORREF fg = logicalEnabled ? C_TEXT : C_AUTO_OFF_TEXT;

    if (id == IDC_PLAY) {
        if (g_playing) {
            RECT a{ cx - 5, cy - 7, cx - 1, cy + 7 };
            RECT b{ cx + 2, cy - 7, cx + 6, cy + 7 };
            FillSolid(dc, a, fg);
            FillSolid(dc, b, fg);
        } else {
            DrawTriangle(dc, {cx - 5, cy - 8}, {cx - 5, cy + 8}, {cx + 8, cy}, fg);
        }
        return;
    }
    if (id == IDC_STOP) {
        RECT s{ cx - 6, cy - 6, cx + 6, cy + 6 };
        FillSolid(dc, s, fg);
        return;
    }
    if (id == IDC_PREV || id == IDC_NEXT) {
        const bool next = id == IDC_NEXT;
        const int dir = next ? 1 : -1;
        const int barX = cx + dir * 7;
        DrawLine(dc, barX, cy - 7, barX, cy + 7, fg, 2);
        if (next) DrawTriangle(dc, {cx - 6, cy - 7}, {cx - 6, cy + 7}, {cx + 5, cy}, fg);
        else DrawTriangle(dc, {cx + 6, cy - 7}, {cx + 6, cy + 7}, {cx - 5, cy}, fg);
        return;
    }
    if (id == IDC_SEEK_BACK || id == IDC_SEEK_FORWARD) {
        const bool next = id == IDC_SEEK_FORWARD;
        if (next) {
            DrawTriangle(dc, {cx - 8, cy - 6}, {cx - 8, cy + 6}, {cx, cy}, fg);
            DrawTriangle(dc, {cx, cy - 6}, {cx, cy + 6}, {cx + 8, cy}, fg);
        } else {
            DrawTriangle(dc, {cx + 8, cy - 6}, {cx + 8, cy + 6}, {cx, cy}, fg);
            DrawTriangle(dc, {cx, cy - 6}, {cx, cy + 6}, {cx - 8, cy}, fg);
        }
        return;
    }
    if (id == IDC_SPEED_DOWN) {
        RECT minus{ cx - 6, cy - 1, cx + 7, cy + 2 };
        FillSolid(dc, minus, fg);
        return;
    }
    if (id == IDC_SPEED_UP) {
        RECT horiz{ cx - 6, cy - 1, cx + 7, cy + 2 };
        RECT vert{ cx - 1, cy - 6, cx + 2, cy + 7 };
        FillSolid(dc, horiz, fg);
        FillSolid(dc, vert, fg);
        return;
    }
    if (id == IDC_LOOP_CURRENT) {
        const COLORREF loopColor = g_loopCurrentFile ? fg : C_AUTO_OFF_TEXT;

        // Exact 26x26 silhouette supplied for the LOOP button artwork. The
        // On/Off references have identical geometry and differ only in colour.
        // Rendering the silhouette as a mask preserves the normal native
        // button background/hover/pressed states.
        static constexpr unsigned int kLoopGlyphRows[26] = {
            0x0000000u, 0x0000000u, 0x0000000u, 0x0010000u, 0x0070000u,
            0x00F0000u, 0x03FFF80u, 0x07FFFC0u, 0x03FFFE0u, 0x00F00E0u,
            0x00700E0u, 0x00100E0u, 0x00000E0u, 0x0000000u, 0x0380000u,
            0x0380400u, 0x0380700u, 0x0380780u, 0x03FFFE0u, 0x01FFFF0u,
            0x00FFFE0u, 0x0000780u, 0x0000700u, 0x0000400u, 0x0000000u,
            0x0000000u
        };

        constexpr int glyphSize = 26;
        const int x0 = r.left + ((r.right - r.left) - glyphSize) / 2;
        const int y0 = r.top + ((r.bottom - r.top) - glyphSize) / 2;
        HBRUSH glyphBrush = CreateSolidBrush(loopColor);
        if (glyphBrush) {
            for (int y = 0; y < glyphSize; ++y) {
                const unsigned int row = kLoopGlyphRows[y];
                int x = 0;
                while (x < glyphSize) {
                    while (x < glyphSize && (row & (1u << x)) == 0) ++x;
                    if (x >= glyphSize) break;
                    const int runStart = x;
                    while (x < glyphSize && (row & (1u << x)) != 0) ++x;
                    RECT run{ x0 + runStart, y0 + y, x0 + x, y0 + y + 1 };
                    FillRect(dc, &run, glyphBrush);
                }
            }
            DeleteObject(glyphBrush);
        }
        return;
    }
    if (id == IDC_PLAYLIST_MENU) {
        DrawTriangle(dc, {cx - 6, cy - 3}, {cx + 6, cy - 3}, {cx, cy + 4}, fg);
        return;
    }
    if (id == IDC_MUTE) {
        // Minimal speaker based on the user's reference. No sound-wave lines.
        // When muted, the entire icon (speaker + slash) turns teal.
        const COLORREF iconColor = g_muted ? C_TEAL : fg;
        const int sx = cx - 1;

        RECT body{ sx - 8, cy - 4, sx - 4, cy + 5 };
        FillSolid(dc, body, iconColor);

        POINT spk[] = {
            {sx - 4, cy - 4},
            {sx + 2, cy - 8},
            {sx + 2, cy + 8},
            {sx - 4, cy + 4}
        };
        HBRUSH br = CreateSolidBrush(iconColor);
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, br));
        HPEN pen = CreatePen(PS_SOLID, 1, iconColor);
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
        Polygon(dc, spk, 4);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(br);

        if (g_muted) {
            DrawLine(dc, sx - 8, cy - 8, sx + 8, cy + 8, C_TEAL, 2);
        }
        return;
    }

    std::wstring text;
    if (id == IDC_AUDIO) text = L"AUD";
    else if (id == IDC_SUBS) text = L"SUB";
    else if (id == IDC_AUTO_NEXT) text = L"AUTO";
    else if (id == IDC_PLAYLIST_OSD) text = L"PLAYLIST";
    else if (id == IDC_PLAYLIST_PREV_BUTTON) text = L"PREV";
    else if (id == IDC_PLAYLIST_NEXT_BUTTON) text = L"NEXT";
    else if (id == IDC_CUT_OPEN) text = L"OPEN";
    else if (id == IDC_CUT_MULTI) {
        text = g_cutInputs.size() > 1
            ? L"MULTI " + std::to_wstring(g_cutInputs.size())
            : L"MULTI";
    }
    else if (id == IDC_CUT_TRACKS) text = L"TRACKS";
    else if (id == IDC_CUT_AUDIO_SYNC) text = L"AUD SYNC";
    else if (id == IDC_CUT_MARK_A) text = L"A";
    else if (id == IDC_CUT_MARK_B) text = L"B";
    else if (id == IDC_CUT_FRAME_PREV) text = L"◀F";
    else if (id == IDC_CUT_FRAME_NEXT) text = L"F▶";
    else if (id == IDC_CUT_CROP) text = g_cutCropActive ? L"CROP*" : L"CROP";
    else if (id == IDC_CUT_AUDIO_OFFSET_MINUS) text = L"-";
    else if (id == IDC_CUT_AUDIO_OFFSET_PLUS) text = L"+";
    else if (id == IDC_AUDIO_SYNC_OFFSET_RESET) text = L"RESET";
    else if (id == IDC_CUT_STICK_KEYFRAMES) text = L"STICK KF";
    else if (id == IDC_CUT_TWO_PASS) text = L"2 PASS";
    else if (id == IDC_CUT_SUBTITLES) text = CutterHasIncludedSubtitleTracks() ? L"SUBS OPT*" : L"SUBS OPT";
    else if (id == IDC_CUT_SUB_SYNC) text = L"SUBS SYNC";
    else if (id == IDC_SUB_ENABLE) text = g_cutSubtitlesEnabled ? L"EXT ON" : L"EXT OFF";
    else if (id == IDC_SUB_BROWSE) text = L"BROWSE";
    else if (id == IDC_SUB_CLEAR) text = L"CLEAR";
    else if (id == IDC_SUB_SOFT) text = L"SOFT";
    else if (id == IDC_SUB_BURN) text = L"BURN";
    else if (id == IDC_SUB_KEEP_EMBEDDED) text = g_cutKeepEmbeddedSubtitles ? L"KEEP EMB*" : L"KEEP EMB";
    else if (id == IDC_SUB_OFFSET_MINUS) text = L"-";
    else if (id == IDC_SUB_OFFSET_PLUS) text = L"+";
    else if (id == IDC_SUB_OFFSET_RESET) text = L"RESET";
    else if (id == IDC_SUB_SYNC_A) text = L"SYNC A";
    else if (id == IDC_SUB_SYNC_B) text = L"SYNC B";
    else if (id == IDC_SUB_DRIFT) text = g_cutSubtitleDriftEnabled ? L"DRIFT*" : L"DRIFT";
    else if (id == IDC_SUB_SYNC_CLEAR) text = L"CLR SYNC";
    else if (id == IDC_CUT_OPTIONS) text = (g_cutOverwriteEnabled || !g_cutShowPowerShell || !g_cutClosePowerShellWhenDone) ? L"OPTIONS*" : L"OPTIONS";
    else if (id == IDC_CUT_OUTPUT) text = L"OUTPUT";
    else if (id == IDC_CUT_CLEAR) text = L"CLEAR";
    else if (id == IDC_CUT_NUKE) text = L"NUKE";
    else if (id == IDC_CUT_CONVERT) text = g_cutWorkerProcess ? L"RUNNING" : L"CONVERT";
    else {
        wchar_t caption[128]{};
        GetWindowTextW(hwnd, caption, static_cast<int>(std::size(caption)));
        text = caption;
    }

    COLORREF textColor = fg;
    if (id == IDC_AUTO_NEXT && !g_autoPlayNextFile) textColor = C_AUTO_OFF_TEXT;
    if (id == IDC_CUT_MARK_A) textColor = C_MARK_A;
    if (id == IDC_CUT_MARK_B) textColor = C_MARK_B;

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, textColor);
    HFONT old = static_cast<HFONT>(SelectObject(dc, g_smallFont));
    RECT tr = r;
    DrawTextW(dc, text.c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
}

LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LONG_PTR state = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    bool hover = (state & 1) != 0;
    bool pressed = (state & 2) != 0;
    const bool active = (state & 4) != 0;
    const int id = GetDlgCtrlID(hwnd);
    const bool logicalEnabled = IsWindowEnabled(hwnd) && CutterButtonLogicallyEnabled(id);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        COLORREF background = pressed ? C_BUTTON_DOWN : C_BUTTON;
        COLORREF hoverBorder = C_HOVER;
        if (id == IDC_CUT_MARK_A) {
            background = pressed ? RGB(18, 60, 32) : C_MARK_A_BG;
            hoverBorder = C_MARK_A;
        } else if (id == IDC_CUT_MARK_B) {
            background = pressed ? RGB(72, 50, 8) : C_MARK_B_BG;
            hoverBorder = C_MARK_B;
        } else if (id == IDC_CUT_CROP && (g_cutCropActive || g_cutCropSelecting)) {
            background = C_CROP_BG;
        } else if (id == IDC_CUT_SUBTITLES &&
                   CutterHasIncludedSubtitleTracks()) {
            background = RGB(0, 52, 54);
            hoverBorder = C_TEAL;
        } else if (id == IDC_CUT_SUB_SYNC && g_cutSubtitleSyncOpen) {
            background = RGB(0, 70, 72);
            hoverBorder = C_TEAL;
        } else if (id == IDC_CUT_AUDIO_SYNC && g_cutAudioSyncOpen) {
            background = RGB(0, 70, 72);
            hoverBorder = C_TEAL;
        } else if (id == IDC_SUB_ENABLE && g_cutSubtitlesEnabled) {
            background = RGB(0, 70, 72);
            hoverBorder = C_TEAL;
        } else if (id == IDC_SUB_SOFT &&
                   g_cutSubtitleMode == CutterSubtitleMode::Soft) {
            background = RGB(0, 70, 72);
            hoverBorder = C_TEAL;
        } else if (id == IDC_SUB_BURN &&
                   g_cutSubtitleMode == CutterSubtitleMode::Burn) {
            background = RGB(0, 70, 72);
            hoverBorder = C_TEAL;
        } else if (id == IDC_SUB_KEEP_EMBEDDED && g_cutKeepEmbeddedSubtitles) {
            background = RGB(0, 70, 72);
            hoverBorder = C_TEAL;
        } else if (id == IDC_SUB_SYNC_A && g_cutSubtitleSyncASet) {
            background = RGB(0, 70, 72);
            hoverBorder = C_TEAL;
        } else if (id == IDC_SUB_SYNC_B && g_cutSubtitleSyncBSet) {
            background = RGB(0, 70, 72);
            hoverBorder = C_TEAL;
        } else if (id == IDC_SUB_DRIFT && g_cutSubtitleDriftEnabled) {
            background = RGB(0, 70, 72);
            hoverBorder = C_TEAL;
        } else if (id == IDC_CUT_STICK_KEYFRAMES && g_cutStickKeyframes) {
            background = RGB(0, 70, 72);
            hoverBorder = C_TEAL;
        } else if (id == IDC_CUT_TWO_PASS && g_cutTwoPass && IsWindowEnabled(hwnd)) {
            background = RGB(0, 70, 72);
            hoverBorder = C_TEAL;
        } else if (active && logicalEnabled) {
            background = RGB(0, 70, 72);
            hoverBorder = C_TEAL;
        } else if (id == IDC_CUT_NUKE) {
            background = pressed ? RGB(78, 18, 18) : C_NUKE_BG;
            hoverBorder = RGB(220, 80, 80);
        } else if (id == IDC_CUT_CONVERT) {
            background = pressed ? RGB(0, 57, 59) : C_CONVERT_BG;
            hoverBorder = RGB(70, 174, 177);
        }
        FillSolid(dc, r, background);
        if (hover && logicalEnabled) {
            HPEN pen = CreatePen(PS_SOLID, 1, hoverBorder);
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
            HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
            Rectangle(dc, r.left, r.top, r.right, r.bottom);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(pen);
        }
        DrawButtonGlyph(hwnd, dc, id, r, logicalEnabled);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!logicalEnabled) return 0;
        if (!hover) {
            state |= 1;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, state);
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        state &= ~1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, state);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        if (!logicalEnabled) return 0;
        SetCapture(hwnd);
        state |= 2;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, state);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        if (!logicalEnabled) return 0;
        const bool wasPressed = (state & 2) != 0;
        state &= ~2;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, state);
        if (GetCapture() == hwnd) ReleaseCapture();
        RECT r{};
        GetClientRect(hwnd, &r);
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (wasPressed && PtInRect(&r, p)) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_CAPTURECHANGED:
        state &= ~2;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, state);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ApplySeekFromX(HWND hwnd, int x) {
    if (g_duration <= 0.0) return;
    RECT r{};
    GetClientRect(hwnd, &r);
    const int width = std::max(1, static_cast<int>(r.right - r.left - 2));
    const int local = std::clamp(x - 1, 0, width);
    SeekAbsolute((static_cast<double>(local) / width) * g_duration);
}

LRESULT CALLBACK SeekProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        FillRect(dc, &r, g_seekBgBrush);
        if (g_duration > 0.0) {
            const double frac = std::clamp(g_timePos / g_duration, 0.0, 1.0);
            const int usable = std::max(1, static_cast<int>(r.right - r.left - 2));
            const int x = 1 + static_cast<int>(frac * usable + 0.5);

            // Fill the elapsed portion subtly, MPC-style, while keeping the overall bar dark.
            RECT elapsed{ r.left, r.top, static_cast<LONG>(std::clamp(x, static_cast<int>(r.left), static_cast<int>(r.right))), r.bottom };
            if (elapsed.right > elapsed.left) {
                FillRect(dc, &elapsed, g_seekFillBrush);
            }

            // MPC-style chapter markers: a thin grey line at each chapter boundary.
            // Cached brushes avoid creating/destroying one GDI brush per marker.
            for (double chapterTime : g_chapterTimes) {
                if (chapterTime <= 0.001 || chapterTime >= g_duration - 0.001) continue;
                const double chapterFrac =
                    std::clamp(chapterTime / g_duration, 0.0, 1.0);
                const int chapterX =
                    1 + static_cast<int>(chapterFrac * usable + 0.5);
                RECT marker{
                    chapterX,
                    r.top,
                    chapterX + 1,
                    r.bottom
                };
                FillRect(dc, &marker, g_chapterMarkerBrush);
            }

            // Cutter A/B markers ride on the proven MPV seek bar. A is green,
            // B is amber; the selected span gets a subtle top-edge accent.
            auto markerX = [&](double seconds) {
                const double markerFrac = std::clamp(seconds / g_duration, 0.0, 1.0);
                return 1 + static_cast<int>(markerFrac * usable + 0.5);
            };
            if (g_cutMarkA >= 0.0 && g_cutMarkB >= 0.0 && g_cutMarkB > g_cutMarkA) {
                const int ax = markerX(g_cutMarkA);
                const int bx = markerX(g_cutMarkB);
                RECT span{ ax, r.top, bx, std::min<LONG>(r.bottom, r.top + 3) };
                FillSolid(dc, span, RGB(70, 80, 50));
            }
            if (g_cutMarkA >= 0.0) {
                const int ax = markerX(g_cutMarkA);
                RECT marker{ ax - 1, r.top, ax + 2, r.bottom };
                FillSolid(dc, marker, C_MARK_A);
            }
            if (g_cutMarkB >= 0.0) {
                const int bx = markerX(g_cutMarkB);
                RECT marker{ bx - 1, r.top, bx + 2, r.bottom };
                FillSolid(dc, marker, C_MARK_B);
            }

            // Three-pixel teal playhead: easier to find without becoming a chunky knob.
            RECT line{ x - 1, r.top, x + 2, r.bottom };
            FillRect(dc, &line, g_seekPlayheadBrush);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        g_seekPointerDown = true;
        g_seekDragging = false;
        g_seekMouseDownPoint = POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        // A normal click seeks on release, not on button-down. This avoids the
        // old two-seek "twitch" while still letting playback continue.
        return 0;
    case WM_MOUSEMOVE:
        if (g_seekPointerDown && GetCapture() == hwnd) {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (!g_seekDragging) {
                const int dragX = std::max(1, GetSystemMetrics(SM_CXDRAG));
                const int dragY = std::max(1, GetSystemMetrics(SM_CYDRAG));
                if (std::abs(x - g_seekMouseDownPoint.x) >= dragX ||
                    std::abs(y - g_seekMouseDownPoint.y) >= dragY) {
                    g_seekDragging = true;
                }
            }
            if (g_seekDragging) ApplySeekFromX(hwnd, x);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_seekPointerDown) {
            // A click seeks exactly once here. A drag has already sought live
            // on mouse movement, so release only ends the interaction.
            if (!g_seekDragging) ApplySeekFromX(hwnd, GET_X_LPARAM(lParam));
            g_seekPointerDown = false;
            g_seekDragging = false;
            if (GetCapture() == hwnd) ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        g_seekPointerDown = false;
        g_seekDragging = false;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        FillSolid(dc, r, C_PROGRESS_BG);
        const int width = std::max(0, static_cast<int>(r.right - r.left));
        if (g_cutWorkerProcess && g_cutProgressIndeterminate && width > 0) {
            const int block = std::max(16, width / 5);
            const int travel = width + block;
            const int left = (g_cutProgressPulse * 18) % std::max(1, travel) - block;
            RECT fill{ std::max<LONG>(r.left, left), r.top,
                       std::min<LONG>(r.right, left + block), r.bottom };
            if (fill.right > fill.left) FillSolid(dc, fill, C_PROGRESS_FILL);
        } else if (g_cutProgressFraction > 0.0 && width > 0) {
            const int fillWidth = static_cast<int>(std::clamp(g_cutProgressFraction, 0.0, 1.0) * width + 0.5);
            RECT fill{ r.left, r.top, r.left + fillWidth, r.bottom };
            FillSolid(dc, fill, C_PROGRESS_FILL);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ApplyVolumeFromX(HWND hwnd, int x) {
    RECT r{};
    GetClientRect(hwnd, &r);
    const int usable = std::max(1, static_cast<int>(r.right - r.left - 2));
    const int local = std::clamp(x - 1, 0, usable);
    const int value = static_cast<int>((static_cast<long long>(local) * 100 + usable / 2) / usable);
    SetVolume(value);
}

LRESULT CALLBACK VolumeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        FillSolid(dc, r, C_BLACK);
        RECT inner{ r.left + 1, r.top + 1, r.right - 1, r.bottom - 1 };
        FillSolid(dc, inner, C_VOLUME_BG);
        const int innerW = std::max(0, static_cast<int>(inner.right - inner.left));
        RECT fill = inner;
        fill.right = fill.left + (innerW * g_volumePercent) / 100;
        if (fill.right > fill.left) FillSolid(dc, fill, C_VOLUME_FILL);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        g_volumeDragging = true;
        ApplyVolumeFromX(hwnd, GET_X_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (g_volumeDragging && GetCapture() == hwnd) ApplyVolumeFromX(hwnd, GET_X_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (g_volumeDragging) {
            ApplyVolumeFromX(hwnd, GET_X_LPARAM(lParam));
            g_volumeDragging = false;
            if (GetCapture() == hwnd) ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        g_volumeDragging = false;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK MenuProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT r{};
        GetClientRect(hwnd, &r);
        FillSolid(reinterpret_cast<HDC>(wParam), r, C_PANEL);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        FillSolid(dc, r, C_PANEL);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, C_TEXT);
        HFONT old = static_cast<HFONT>(SelectObject(dc, g_font));
        static constexpr const wchar_t* labels[] = { L"File", L"View", L"Play", L"Help" };
        for (int i = 0; i < 4; ++i) {
            RECT item = MenuItemRect(i);
            if (i == g_menuHover) FillSolid(dc, item, RGB(48, 48, 48));
            RECT tr = item;
            tr.left += 6;
            DrawTextW(dc, labels[i], -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(dc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int hit = HitTestMenu(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (hit != g_menuHover) {
            g_menuHover = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_menuHover = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        const int hit = HitTestMenu(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (hit >= 0) ShowTopMenu(hit);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT r{};
        GetClientRect(hwnd, &r);
        FillSolid(reinterpret_cast<HDC>(wParam), r, C_PANEL);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        FillSolid(dc, r, C_PANEL);
        DrawLine(dc, 0, 0, r.right, 0, C_PANEL_EDGE, 1);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, C_PANEL);
        const HWND control = reinterpret_cast<HWND>(lParam);
        const bool dim = control == g_status || control == g_audioInfo || control == g_subInfo;
        SetTextColor(dc, dim ? C_TEXT_DIM : C_TEXT);
        return reinterpret_cast<LRESULT>(g_panelBrush);
    }
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_PANEL);
        return reinterpret_cast<LRESULT>(g_panelBrush);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, RGB(28, 28, 28));
        return reinterpret_cast<LRESULT>(g_editBrush ? g_editBrush : g_panelBrush);
    }
    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (measure && (measure->CtlID == IDC_CUT_FORMAT ||
                        measure->CtlID == IDC_CUT_RESOLUTION ||
                        measure->CtlID == IDC_CUT_AUDIO_MODE ||
                        measure->CtlID == IDC_AUDIO_SYNC_TRACK ||
                        measure->CtlID == IDC_SUB_SYNC_TRACK)) {
            measure->itemHeight = 20;
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw && (draw->CtlID == IDC_CUT_FORMAT ||
                     draw->CtlID == IDC_CUT_RESOLUTION ||
                     draw->CtlID == IDC_CUT_AUDIO_MODE ||
                     draw->CtlID == IDC_AUDIO_SYNC_TRACK ||
                     draw->CtlID == IDC_SUB_SYNC_TRACK)) {
            const bool selected = (draw->itemState & ODS_SELECTED) != 0;
            const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
            const COLORREF background = selected ? RGB(52, 52, 52) : RGB(28, 28, 28);

            HBRUSH brush = CreateSolidBrush(background);
            FillRect(draw->hDC, &draw->rcItem, brush);
            DeleteObject(brush);

            wchar_t text[128]{};
            int item = static_cast<int>(draw->itemID);
            if (item < 0) {
                item = static_cast<int>(
                    SendMessageW(draw->hwndItem, CB_GETCURSEL, 0, 0));
            }
            if (item >= 0) {
                SendMessageW(draw->hwndItem, CB_GETLBTEXT,
                             static_cast<WPARAM>(item),
                             reinterpret_cast<LPARAM>(text));
            }

            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, disabled ? C_AUTO_OFF_TEXT : C_TEXT);
            HFONT oldFont = nullptr;
            if (g_smallFont) {
                oldFont = static_cast<HFONT>(SelectObject(draw->hDC, g_smallFont));
            }

            RECT textRect = draw->rcItem;
            textRect.left += 7;
            textRect.right -= 4;
            DrawTextW(draw->hDC, text, -1, &textRect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            if (oldFont) SelectObject(draw->hDC, oldFont);
            if ((draw->itemState & ODS_FOCUS) != 0) {
                RECT focus = draw->rcItem;
                InflateRect(&focus, -1, -1);
                DrawFocusRect(draw->hDC, &focus);
            }
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        SendMessageW(g_main, WM_COMMAND, wParam, lParam);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK VideoProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT r{};
        GetClientRect(hwnd, &r);
        FillSolid(reinterpret_cast<HDC>(wParam), r, C_BG);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        if (!g_mpvReady || MpvGetFlag("idle-active", true)) {
            FillSolid(dc, r, C_BG);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(95, 95, 95));
            HFONT old = static_cast<HFONT>(SelectObject(dc, g_font));
            const wchar_t* idleText = g_initializingPlaybackEngine
                ? L"WinterStatic Video Cutter\nInitializing playback engine..."
                : L"WinterStatic Video Cutter\nMPV-powered cutter";
            DrawTextW(dc, idleText, -1, &r,
                      DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, old);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const bool visualCropOverlay = hwnd == g_cropOverlay;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        // The normal overlay is the original almost-transparent alpha=1 mouse
        // surface used by the MPV frontend. A second click-through colour-keyed
        // overlay draws the crop rectangle at full opacity without stealing input.
        FillSolid(dc, r, C_BLACK);

        if (visualCropOverlay && (g_cutCropSelecting || g_cutCropActive)) {
            RECT crop{};
            if (g_cutCropDragging &&
                g_cutCropDragMode == CutterCropDragMode::NewRect) {
                crop.left = std::min(g_cutCropStart.x, g_cutCropCurrent.x);
                crop.top = std::min(g_cutCropStart.y, g_cutCropCurrent.y);
                crop.right = std::max(g_cutCropStart.x, g_cutCropCurrent.x);
                crop.bottom = std::max(g_cutCropStart.y, g_cutCropCurrent.y);
            } else if (g_cutCropActive && g_cutCrop.valid && g_cutVideoInfo.valid) {
                crop = CutterCropOverlayRect();
            }

            if (crop.right > crop.left && crop.bottom > crop.top) {
                HPEN pen = CreatePen(PS_SOLID, 2, C_MARK_A);
                HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
                HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
                Rectangle(dc, crop.left, crop.top, crop.right, crop.bottom);
                SelectObject(dc, oldPen);
                SelectObject(dc, oldBrush);
                DeleteObject(pen);

                if (g_cutCropSelecting && g_cutCropActive) {
                    constexpr int hs = 4;
                    const LONG cx = (crop.left + crop.right) / 2;
                    const LONG cy = (crop.top + crop.bottom) / 2;
                    const POINT handles[] = {
                        { crop.left, crop.top }, { cx, crop.top }, { crop.right, crop.top },
                        { crop.left, cy },                         { crop.right, cy },
                        { crop.left, crop.bottom }, { cx, crop.bottom }, { crop.right, crop.bottom }
                    };
                    HBRUSH handleBrush = CreateSolidBrush(C_TEAL);
                    for (const POINT& h : handles) {
                        RECT hr{ h.x - hs, h.y - hs, h.x + hs + 1, h.y + hs + 1 };
                        FillRect(dc, &hr, handleBrush);
                    }
                    DeleteObject(handleBrush);
                }
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        if (visualCropOverlay) return HTTRANSPARENT;
        break;
    case WM_LBUTTONDOWN:
        NoteFullscreenMouseActivity();
        if (g_cutCropSelecting) {
            const POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            CutterCropDragMode mode = HitTestCutterCrop(p);
            if (mode == CutterCropDragMode::None) {
                mode = CutterCropDragMode::NewRect;
            }

            SetCapture(hwnd);
            g_cutCropDragging = true;
            g_cutCropDragMode = mode;
            g_cutCropDragAnchor = p;

            if (mode == CutterCropDragMode::NewRect) {
                g_cutCropStart = p;
                g_cutCropCurrent = p;
            } else {
                g_cutCropDragOriginRect = CutterCropOverlayRect();
            }

            SetCursor(CutterCropCursor(mode));
            InvalidateRect(g_cropOverlay ? g_cropOverlay : hwnd, nullptr, TRUE);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        NoteFullscreenMouseActivity();
        if (g_cutCropSelecting) {
            if (g_cutCropDragging) {
                const POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (g_cutCropDragMode == CutterCropDragMode::NewRect) {
                    g_cutCropCurrent = p;
                    const int dx = std::abs(g_cutCropCurrent.x - g_cutCropStart.x);
                    const int dy = std::abs(g_cutCropCurrent.y - g_cutCropStart.y);
                    if (dx >= 2 && dy >= 2) {
                        SetCutterCropFromOverlayPoints(g_cutCropStart, g_cutCropCurrent);
                        g_cutCropActive = g_cutCrop.valid;
                    }
                } else {
                    UpdateCutterCropResize(p);
                }

                if (GetCapture() == hwnd) ReleaseCapture();
                g_cutCropDragging = false;
                g_cutCropDragMode = CutterCropDragMode::None;

                if (g_cutCropActive) {
                    ShowOsdText(L"Crop " + std::to_wstring(g_cutCrop.w) + L"x" +
                                std::to_wstring(g_cutCrop.h) + L"  x:" +
                                std::to_wstring(g_cutCrop.x) + L" y:" +
                                std::to_wstring(g_cutCrop.y) +
                                L"  (drag handles to resize)", 2400);
                }
            }

            const POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            SetCursor(CutterCropCursor(HitTestCutterCrop(p)));
            SyncCutterControls();
            InvalidateRect(g_cropOverlay ? g_cropOverlay : hwnd, nullptr, TRUE);
            return 0;
        }
        if (g_ignoreNextVideoButtonUp) {
            g_ignoreNextVideoButtonUp = false;
            return 0;
        }
        // MPC-like compromise: single-click responds almost immediately, but a fast double-click
        // still gets fullscreen without leaving playback toggled.
        g_singleClickApplied = false;
        g_pendingSingleClick = true;
        SetTimer(g_main, TIMER_SINGLE_CLICK, g_singleClickDelayMs, nullptr);
        return 0;
    case WM_LBUTTONDBLCLK:
        NoteFullscreenMouseActivity();
        if (g_cutCropSelecting) return 0;
        if (g_pendingSingleClick) {
            KillTimer(g_main, TIMER_SINGLE_CLICK);
            g_pendingSingleClick = false;
        } else if (g_singleClickApplied) {
            // If the shorter timer already fired, undo that one toggle before fullscreen.
            TogglePlay();
        }
        g_singleClickApplied = false;
        g_ignoreNextVideoButtonUp = true;
        ToggleFullscreen();
        return 0;
    case WM_RBUTTONDOWN:
        NoteFullscreenMouseActivity();
        if (g_cutCropSelecting) return 0;
        break;
    case WM_RBUTTONUP: {
        NoteFullscreenMouseActivity();
        if (g_cutCropSelecting) {
            return 0;
        }
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &p);
        ShowContextMenu(p);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_cutCropSelecting && g_cutCropDragging && GetCapture() == hwnd) {
            const POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (g_cutCropDragMode == CutterCropDragMode::NewRect) {
                g_cutCropCurrent = p;
            } else {
                UpdateCutterCropResize(p);
            }
            SetCursor(CutterCropCursor(g_cutCropDragMode));
            InvalidateRect(g_cropOverlay ? g_cropOverlay : hwnd, nullptr, TRUE);
            return 0;
        }
        if (g_cutCropSelecting) {
            const POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            SetCursor(CutterCropCursor(HitTestCutterCrop(p)));
        }
        if (g_fullscreen) {
            NoteFullscreenMouseActivity();
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &p);
            ScreenToClient(g_main, &p);
            RECT r{};
            GetClientRect(g_main, &r);
            if (!g_controlsVisible && p.y >= r.bottom - kRevealEdge) SetControlsVisible(true);
        } else {
            NoteWindowedVideoMouseActivity();
            if (!g_trackingVideoMouseLeave) {
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
                if (TrackMouseEvent(&tme)) g_trackingVideoMouseLeave = true;
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (!g_fullscreen) {
            g_trackingVideoMouseLeave = false;
            g_cursorOverVideo = false;
            g_haveWindowedVideoMouseAnchor = false;
            SetWindowedCursorHidden(false);
        }
        return 0;
    case WM_SETCURSOR:
        if (((g_fullscreen && g_fullscreenCursorHidden) ||
             (!g_fullscreen && g_windowedCursorHidden)) &&
            LOWORD(lParam) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND CreateButton(HWND parent, int id) {
    return CreateWindowExW(0, kButtonClass, nullptr,
                           WS_CHILD | WS_VISIBLE,
                           0, 0, 26, 26, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
}

void SetCustomButtonActive(HWND button, bool active) {
    if (!button) return;
    LONG_PTR state = GetWindowLongPtrW(button, GWLP_USERDATA);
    if (active) state |= 4;
    else state &= ~static_cast<LONG_PTR>(4);
    SetWindowLongPtrW(button, GWLP_USERDATA, state);
    InvalidateRect(button, nullptr, FALSE);
}

HWND CreateDialogCustomButton(HWND dialog, int id, const wchar_t* text,
                              int xDlu, int yDlu, int widthDlu, int heightDlu) {
    RECT r{ xDlu, yDlu, xDlu + widthDlu, yDlu + heightDlu };
    MapDialogRect(dialog, &r);
    HWND button = CreateButton(dialog, id);
    if (!button) return nullptr;
    SetWindowTextW(button, text ? text : L"");
    MoveWindow(button, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    return button;
}

void CreateUi(HWND hwnd) {
    g_menuBar = CreateWindowExW(0, kMenuClass, nullptr, WS_CHILD | WS_VISIBLE,
                                0, 0, 100, kMenuHeight, hwnd, nullptr, g_instance, nullptr);
    g_video = CreateWindowExW(0, kVideoClass, nullptr,
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                              0, 0, 100, 100, hwnd, nullptr, g_instance, nullptr);
    g_overlay = CreateWindowExW(WS_EX_LAYERED, kOverlayClass, nullptr,
                                WS_CHILD | WS_VISIBLE,
                                0, 0, 100, 100, g_video, nullptr, g_instance, nullptr);
    if (g_overlay) SetLayeredWindowAttributes(g_overlay, 0, 1, LWA_ALPHA);
    g_cropOverlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT,
                                    kOverlayClass, nullptr,
                                    WS_CHILD | WS_VISIBLE,
                                    0, 0, 100, 100, g_video, nullptr, g_instance, nullptr);
    if (g_cropOverlay) SetLayeredWindowAttributes(g_cropOverlay, C_BLACK, 0, LWA_COLORKEY);
    g_panel = CreateWindowExW(0, kPanelClass, nullptr,
                              WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                              0, 0, 100, kPanelBaseHeight, hwnd, nullptr, g_instance, nullptr);

    g_seek = CreateWindowExW(0, kSeekClass, nullptr, WS_CHILD | WS_VISIBLE,
                             0, 0, 100, kSeekHeight, g_panel,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SEEK)), g_instance, nullptr);
    g_cutProgress = CreateWindowExW(0, kProgressClass, nullptr, WS_CHILD | WS_VISIBLE,
                                    0, 0, 100, 8, g_panel, nullptr, g_instance, nullptr);

    g_play = CreateButton(g_panel, IDC_PLAY);
    g_stop = CreateButton(g_panel, IDC_STOP);
    g_prev = CreateButton(g_panel, IDC_PREV);
    g_seekBack = CreateButton(g_panel, IDC_SEEK_BACK);
    g_seekForward = CreateButton(g_panel, IDC_SEEK_FORWARD);
    g_next = CreateButton(g_panel, IDC_NEXT);
    g_speedDown = CreateButton(g_panel, IDC_SPEED_DOWN);
    g_speedUp = CreateButton(g_panel, IDC_SPEED_UP);
    g_autoNext = CreateButton(g_panel, IDC_AUTO_NEXT);
    g_loopCurrent = CreateButton(g_panel, IDC_LOOP_CURRENT);
    g_playlistPrevButton = CreateButton(g_panel, IDC_PLAYLIST_PREV_BUTTON);
    g_playlistNextButton = CreateButton(g_panel, IDC_PLAYLIST_NEXT_BUTTON);
    g_playlistOsd = CreateButton(g_panel, IDC_PLAYLIST_OSD);
    g_playlistMenu = CreateButton(g_panel, IDC_PLAYLIST_MENU);
    g_audio = CreateButton(g_panel, IDC_AUDIO);
    g_subs = CreateButton(g_panel, IDC_SUBS);
    g_mute = CreateButton(g_panel, IDC_MUTE);

    // Cutter row. The existing MPV transport remains untouched above it.
    g_cutOpen = CreateButton(g_panel, IDC_CUT_OPEN);
    g_cutMulti = CreateButton(g_panel, IDC_CUT_MULTI);
    g_cutTracks = CreateButton(g_panel, IDC_CUT_TRACKS);
    g_cutAudioSyncButton = CreateButton(g_panel, IDC_CUT_AUDIO_SYNC);
    g_cutMarkAButton = CreateButton(g_panel, IDC_CUT_MARK_A);
    g_cutMarkBButton = CreateButton(g_panel, IDC_CUT_MARK_B);
    g_cutFramePrev = CreateButton(g_panel, IDC_CUT_FRAME_PREV);
    g_cutFrameNext = CreateButton(g_panel, IDC_CUT_FRAME_NEXT);
    g_cutCropButton = CreateButton(g_panel, IDC_CUT_CROP);
    g_cutSubtitles = CreateButton(g_panel, IDC_CUT_SUBTITLES);
    g_cutSubSyncButton = CreateButton(g_panel, IDC_CUT_SUB_SYNC);
    g_cutStickKeyframesButton = CreateButton(g_panel, IDC_CUT_STICK_KEYFRAMES);
    g_cutAudioOffsetMinus = CreateButton(g_panel, IDC_CUT_AUDIO_OFFSET_MINUS);
    g_cutAudioOffsetPlus = CreateButton(g_panel, IDC_CUT_AUDIO_OFFSET_PLUS);
    g_cutAudioOffsetReset = CreateButton(g_panel, IDC_AUDIO_SYNC_OFFSET_RESET);
    g_cutOptions = CreateButton(g_panel, IDC_CUT_OPTIONS);
    g_cutOutput = CreateButton(g_panel, IDC_CUT_OUTPUT);
    g_cutClear = CreateButton(g_panel, IDC_CUT_CLEAR);
    g_cutNuke = CreateButton(g_panel, IDC_CUT_NUKE);
    g_cutConvert = CreateButton(g_panel, IDC_CUT_CONVERT);

    // Inline subtitle context controls. They are direct children of g_panel so
    // they share the same dark-theme/owner-draw behavior as the rest of Cutter.
    g_cutSubEnable = CreateButton(g_panel, IDC_SUB_ENABLE);
    g_cutSubBrowse = CreateButton(g_panel, IDC_SUB_BROWSE);
    g_cutSubClear = CreateButton(g_panel, IDC_SUB_CLEAR);
    g_cutSubSoft = CreateButton(g_panel, IDC_SUB_SOFT);
    g_cutSubBurn = CreateButton(g_panel, IDC_SUB_BURN);
    g_cutSubKeepEmbedded = CreateButton(g_panel, IDC_SUB_KEEP_EMBEDDED);
    g_cutSubOffsetMinus = CreateButton(g_panel, IDC_SUB_OFFSET_MINUS);
    g_cutSubOffsetPlus = CreateButton(g_panel, IDC_SUB_OFFSET_PLUS);
    g_cutSubOffsetReset = CreateButton(g_panel, IDC_SUB_OFFSET_RESET);
    g_cutSubSyncA = CreateButton(g_panel, IDC_SUB_SYNC_A);
    g_cutSubSyncB = CreateButton(g_panel, IDC_SUB_SYNC_B);
    g_cutSubDrift = CreateButton(g_panel, IDC_SUB_DRIFT);
    g_cutSubSyncClear = CreateButton(g_panel, IDC_SUB_SYNC_CLEAR);

    g_cutSubPathLabel = CreateWindowExW(0, L"STATIC", L"File",
        WS_CHILD | SS_LEFTNOWORDWRAP,
        0, 0, 28, 18, g_panel, nullptr, g_instance, nullptr);
    g_cutSubPathEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 180, 22, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SUB_PATH)), g_instance, nullptr);
    g_cutSubTitleLabel = CreateWindowExW(0, L"STATIC", L"Title",
        WS_CHILD | SS_LEFTNOWORDWRAP,
        0, 0, 32, 18, g_panel, nullptr, g_instance, nullptr);
    g_cutSubTitleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 100, 22, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SUB_TITLE)), g_instance, nullptr);
    g_cutAudioSyncTrackLabel = CreateWindowExW(0, L"STATIC", L"AUD",
        WS_CHILD | SS_LEFTNOWORDWRAP,
        0, 0, 30, 18, g_panel, nullptr, g_instance, nullptr);
    g_cutAudioSyncTrack = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST |
        CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 270, 180, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_AUDIO_SYNC_TRACK)),
        g_instance, nullptr);
    SetWindowTheme(g_cutAudioSyncTrack, L"DarkMode_CFD", nullptr);
    SendMessageW(g_cutAudioSyncTrack, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 20);
    SendMessageW(g_cutAudioSyncTrack, CB_SETITEMHEIGHT, 0, 20);
    g_cutAudioSyncStatus = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS,
        0, 0, 260, 18, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_AUDIO_SYNC_STATUS)),
        g_instance, nullptr);

    g_cutSubSyncTrackLabel = CreateWindowExW(0, L"STATIC", L"SUB",
        WS_CHILD | SS_LEFTNOWORDWRAP,
        0, 0, 28, 18, g_panel, nullptr, g_instance, nullptr);
    g_cutSubSyncTrack = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST |
        CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 210, 180, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SUB_SYNC_TRACK)),
        g_instance, nullptr);
    SetWindowTheme(g_cutSubSyncTrack, L"DarkMode_CFD", nullptr);
    SendMessageW(g_cutSubSyncTrack, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 20);
    SendMessageW(g_cutSubSyncTrack, CB_SETITEMHEIGHT, 0, 20);

    g_cutSubOffsetLabel = CreateWindowExW(0, L"STATIC", L"Offset ms",
        WS_CHILD | SS_LEFTNOWORDWRAP,
        0, 0, 62, 18, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SUB_OFFSET_LABEL)), g_instance, nullptr);
    g_cutSubOffsetEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT,
        0, 0, 62, 22, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SUB_OFFSET)), g_instance, nullptr);
    g_cutSubSyncStatus = CreateWindowExW(0, L"STATIC", L"A --   B --",
        WS_CHILD | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS,
        0, 0, 240, 18, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SUB_SYNC_STATUS)), g_instance, nullptr);

    SyncCutterContextVisibility();

    g_cutFormatLabel = CreateWindowExW(0, L"STATIC", L"Format",
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        0, 0, 42, 18, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_FORMAT_LABEL)), g_instance, nullptr);
    g_cutFormat = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
        CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 108, 180, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_FORMAT)), g_instance, nullptr);
    const wchar_t* cutModes[] = {
        L"Source", L"MKV", L"MP4", L"MP4 H.264", L"WebM", L"Extract audio", L"Audio to M4A"
    };
    for (const wchar_t* mode : cutModes) SendMessageW(g_cutFormat, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode));

    g_cutResLabel = CreateWindowExW(0, L"STATIC", L"Res",
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        0, 0, 24, 18, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_RES_LABEL)), g_instance, nullptr);
    g_cutResolution = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
        CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 76, 150, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_RESOLUTION)), g_instance, nullptr);
    const wchar_t* resolutions[] = { L"Source", L"1080p", L"720p", L"480p", L"360p" };
    for (const wchar_t* resolution : resolutions) SendMessageW(g_cutResolution, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(resolution));

    // Win32 combo boxes otherwise keep a bright white drop-down even in a dark app.
    // Owner-draw handles the list/selection, while the Win11 dark theme styles the
    // border and arrow button.
    SetWindowTheme(g_cutFormat, L"DarkMode_CFD", nullptr);
    SetWindowTheme(g_cutResolution, L"DarkMode_CFD", nullptr);
    SetWindowTheme(g_cutSubSyncTrack, L"DarkMode_CFD", nullptr);
    SendMessageW(g_cutFormat, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 20);
    SendMessageW(g_cutFormat, CB_SETITEMHEIGHT, 0, 20);
    SendMessageW(g_cutResolution, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 20);
    SendMessageW(g_cutResolution, CB_SETITEMHEIGHT, 0, 20);

    g_cutSizeLabel = CreateWindowExW(0, L"STATIC", L"Size KB",
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        0, 0, 48, 18, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_SIZE_LABEL)), g_instance, nullptr);
    g_cutSize = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT,
        0, 0, 68, 22, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_SIZE)), g_instance, nullptr);

    g_cutTwoPassButton = CreateButton(g_panel, IDC_CUT_TWO_PASS);

    g_cutCrfLabel = CreateWindowExW(0, L"STATIC", L"CRF",
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        0, 0, 28, 18, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_CRF_LABEL)), g_instance, nullptr);
    g_cutCrfEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"32",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT | ES_NUMBER,
        0, 0, 38, 22, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_CRF)), g_instance, nullptr);

    g_cutAudioLabel = CreateWindowExW(0, L"STATIC", L"Aud",
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        0, 0, 28, 18, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_AUDIO_LABEL)), g_instance, nullptr);
    g_cutAudioModeCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
        CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 64, 100, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_AUDIO_MODE)), g_instance, nullptr);
    for (const wchar_t* mode : { L"Auto", L"Copy", L"None" }) {
        SendMessageW(g_cutAudioModeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode));
    }
    g_cutAudioKbpsEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"128",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT | ES_NUMBER,
        0, 0, 44, 22, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_AUDIO_KBPS)), g_instance, nullptr);
    SetWindowTheme(g_cutAudioModeCombo, L"DarkMode_CFD", nullptr);
    SendMessageW(g_cutAudioModeCombo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 20);
    SendMessageW(g_cutAudioModeCombo, CB_SETITEMHEIGHT, 0, 20);

    g_cutAudioOffsetLabel = CreateWindowExW(0, L"STATIC", L"Offset ms",
        WS_CHILD | SS_LEFTNOWORDWRAP,
        0, 0, 70, 18, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_AUDIO_OFFSET_LABEL)), g_instance, nullptr);
    g_cutAudioOffset = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT,
        0, 0, 48, 22, g_panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CUT_AUDIO_OFFSET)), g_instance, nullptr);

    g_volume = CreateWindowExW(0, kVolumeClass, nullptr, WS_CHILD | WS_VISIBLE,
                               0, 0, 80, 18, g_panel,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLUME)), g_instance, nullptr);

    g_status = CreateWindowExW(0, L"STATIC", L"Stopped",
                               WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS,
                               0, 0, 120, 18, g_panel,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)), g_instance, nullptr);
    g_audioInfo = CreateWindowExW(0, L"STATIC", L"--",
                                  WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS,
                                  0, 0, 60, 18, g_panel,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_AUDIO_INFO)), g_instance, nullptr);
    g_subInfo = CreateWindowExW(0, L"STATIC", L"--",
                                WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS,
                                0, 0, 60, 18, g_panel,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SUB_INFO)), g_instance, nullptr);
    g_volumeText = CreateWindowExW(0, L"STATIC", L"",
                                   WS_CHILD | WS_VISIBLE | SS_CENTER,
                                   0, 0, 42, 18, g_panel,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLUME_TEXT)), g_instance, nullptr);
    g_time = CreateWindowExW(0, L"STATIC", L"00:00 / 00:00",
                             WS_CHILD | WS_VISIBLE | SS_RIGHT,
                             0, 0, 112, 18, g_panel,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TIME)), g_instance, nullptr);
    ApplyFont(g_status, true);
    ApplyFont(g_audioInfo, true);
    ApplyFont(g_subInfo, true);
    ApplyFont(g_volumeText, true);
    ApplyFont(g_time, true);
    ApplyDarkTheme(g_panel);
    ApplyDarkTheme(g_status);
    ApplyDarkTheme(g_audioInfo);
    ApplyDarkTheme(g_subInfo);
    ApplyDarkTheme(g_volumeText);
    ApplyDarkTheme(g_time);

    HWND cutterControls[] = {
        g_cutFormatLabel, g_cutFormat, g_cutResLabel, g_cutResolution,
        g_cutSizeLabel, g_cutSize, g_cutCrfLabel, g_cutCrfEdit,
        g_cutAudioOffsetLabel, g_cutAudioOffset,
        g_cutAudioSyncTrackLabel, g_cutAudioSyncTrack, g_cutAudioSyncStatus,
        g_cutSubPathLabel, g_cutSubPathEdit,
        g_cutSubTitleLabel, g_cutSubTitleEdit,
        g_cutSubSyncTrackLabel, g_cutSubSyncTrack,
        g_cutSubOffsetLabel, g_cutSubOffsetEdit,
        g_cutSubSyncStatus
    };
    for (HWND control : cutterControls) {
        ApplyFont(control, true);
        ApplyDarkTheme(control);
    }
    // ApplyDarkTheme uses the Explorer theme generally; re-apply the combo-specific
    // dark theme last so the arrow/border do not fall back to a light control.
    SetWindowTheme(g_cutFormat, L"DarkMode_CFD", nullptr);
    SetWindowTheme(g_cutResolution, L"DarkMode_CFD", nullptr);
    SetWindowTheme(g_cutAudioModeCombo, L"DarkMode_CFD", nullptr);
    SetWindowTheme(g_cutAudioSyncTrack, L"DarkMode_CFD", nullptr);
    SetWindowTheme(g_cutSubSyncTrack, L"DarkMode_CFD", nullptr);
    SyncCutterAudioContextControls();
    SyncCutterSubtitleContextControls();
    SyncCutterContextVisibility();
    SyncCutterControls();
    UpdateVolumeLabel();
    UpdateTimeLabel();

    LayoutChildren(hwnd);
}

bool ExecuteShortcutAction(ShortcutAction action) {
    switch (action) {
    case ShortcutAction::OpenFile:
        OpenFileDialog(); return true;
    case ShortcutAction::PlayPause:
        TogglePlay(); return true;
    case ShortcutAction::Stop:
        StopPlayback(); return true;
    case ShortcutAction::PreviousChapter:
        ChapterStep(false); return true;
    case ShortcutAction::NextChapter:
        ChapterStep(true); return true;
    case ShortcutAction::SeekBackward:
        SeekBy(-kSeekStepSeconds); return true;
    case ShortcutAction::SeekForward:
        SeekBy(kSeekStepSeconds); return true;
    case ShortcutAction::SpeedDown:
        AdjustSpeed(-0.1); return true;
    case ShortcutAction::SpeedUp:
        AdjustSpeed(0.1); return true;
    case ShortcutAction::VolumeUp:
        AdjustVolume(kVolumeStep); return true;
    case ShortcutAction::VolumeDown:
        AdjustVolume(-kVolumeStep); return true;
    case ShortcutAction::Mute:
        ToggleMute(); return true;
    case ShortcutAction::ToggleFullscreen:
        ToggleFullscreen(); return true;
    case ShortcutAction::ExitFullscreen:
        if (g_fullscreen) ExitFullscreen();
        return true;
    case ShortcutAction::ToggleAutoNext:
        ToggleAutoPlayNextFile(); return true;
    case ShortcutAction::TogglePlaylist:
        TogglePlaylistOsd(); return true;
    case ShortcutAction::PreviousPlaylist:
        PlaylistStep(false); return true;
    case ShortcutAction::NextPlaylist:
        PlaylistStep(true); return true;
    case ShortcutAction::CycleAudio:
        CycleAudioTrack(); return true;
    case ShortcutAction::CycleSubtitle:
        CycleSubtitleTrack(); return true;
    case ShortcutAction::AudioDelayDown:
        AdjustSyncDelayMs("audio-delay", L"Audio delay", -kSyncStepMs,
                          g_audioDelayTouched); return true;
    case ShortcutAction::AudioDelayUp:
        AdjustSyncDelayMs("audio-delay", L"Audio delay", kSyncStepMs,
                          g_audioDelayTouched); return true;
    case ShortcutAction::ResetAudioDelay:
        SetSyncDelayMs("audio-delay", L"Audio delay", 0,
                       g_audioDelayTouched); return true;
    case ShortcutAction::SubtitleDelayDown:
        AdjustSyncDelayMs("sub-delay", L"Subtitle delay", -kSyncStepMs,
                          g_subtitleDelayTouched); return true;
    case ShortcutAction::SubtitleDelayUp:
        AdjustSyncDelayMs("sub-delay", L"Subtitle delay", kSyncStepMs,
                          g_subtitleDelayTouched); return true;
    case ShortcutAction::ResetSubtitleDelay:
        SetSyncDelayMs("sub-delay", L"Subtitle delay", 0,
                       g_subtitleDelayTouched); return true;
    case ShortcutAction::MediaInfo:
        ToggleMediaInfo(); return true;
    case ShortcutAction::ToggleLoopCurrent:
        ToggleLoopCurrentFile(); return true;
    case ShortcutAction::PreciseSeekBackward:
        PreciseSeekBy(-kSeekStepSeconds); return true;
    case ShortcutAction::PreciseSeekForward:
        PreciseSeekBy(kSeekStepSeconds); return true;
    case ShortcutAction::TakeScreenshot:
        TakeScreenshot(); return true;
    case ShortcutAction::PreviousFrame:
        FrameStep(false); return true;
    case ShortcutAction::NextFrame:
        FrameStep(true); return true;
    case ShortcutAction::Count:
        break;
    }
    return false;
}

bool HandleShortcutBinding(const KeyBinding& pressed) {
    if (pressed.vk == 0) return false;

    for (size_t action = 0; action < kShortcutActionCount; ++action) {
        for (size_t slot = 0; slot < kShortcutSlotCount; ++slot) {
            const KeyBinding& configured = g_shortcuts[action][slot];
            if (configured.vk != 0 && SameKeyBinding(configured, pressed)) {
                return ExecuteShortcutAction(static_cast<ShortcutAction>(action));
            }
        }
    }
    return false;
}

bool IsAppCommandVirtualKey(UINT vk) {
    switch (vk) {
    case VK_MEDIA_PREV_TRACK:
    case VK_MEDIA_NEXT_TRACK:
    case VK_MEDIA_PLAY_PAUSE:
    case VK_MEDIA_STOP:
    case VK_VOLUME_MUTE:
    case VK_VOLUME_DOWN:
    case VK_VOLUME_UP:
        return true;
    default:
        return false;
    }
}

bool HandleMediaShortcutEvent(const KeyBinding& pressed,
                              MediaShortcutSource source) {
    if (!IsAppCommandVirtualKey(pressed.vk)) return false;

    const ULONGLONG now = GetTickCount64();
    if (g_lastHandledMediaVk == pressed.vk &&
        g_lastHandledMediaSource != source &&
        now - g_lastHandledMediaTick <= kMediaShortcutDedupMs) {
        // Many Windows keyboards emit one physical media-key press twice:
        // first as VK_MEDIA_* and again as WM_APPCOMMAND (or vice versa).
        // Consume the second representation so one press runs one action.
        return true;
    }

    const bool handled = HandleShortcutBinding(pressed);
    if (handled) {
        g_lastHandledMediaVk = pressed.vk;
        g_lastHandledMediaTick = now;
        g_lastHandledMediaSource = source;
    }
    return handled;
}

bool HandleShortcut(WPARAM key) {
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 ||
        (GetKeyState(VK_RWIN) & 0x8000) != 0) {
        return false;
    }

    KeyBinding pressed{};
    pressed.vk = static_cast<UINT>(key);
    pressed.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    pressed.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    pressed.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (IsAppCommandVirtualKey(pressed.vk)) {
        return HandleMediaShortcutEvent(pressed, MediaShortcutSource::KeyDown);
    }
    return HandleShortcutBinding(pressed);
}

UINT VirtualKeyFromAppCommand(int appCommand) {
    switch (appCommand) {
    case APPCOMMAND_MEDIA_PREVIOUSTRACK:
        return VK_MEDIA_PREV_TRACK;
    case APPCOMMAND_MEDIA_NEXTTRACK:
        return VK_MEDIA_NEXT_TRACK;
    case APPCOMMAND_MEDIA_PLAY_PAUSE:
        return VK_MEDIA_PLAY_PAUSE;
    case APPCOMMAND_MEDIA_STOP:
        return VK_MEDIA_STOP;
    case APPCOMMAND_VOLUME_MUTE:
        return VK_VOLUME_MUTE;
    case APPCOMMAND_VOLUME_DOWN:
        return VK_VOLUME_DOWN;
    case APPCOMMAND_VOLUME_UP:
        return VK_VOLUME_UP;
    default:
        return 0;
    }
}

bool HandleMediaAppCommand(LPARAM lParam) {
    const UINT vk = VirtualKeyFromAppCommand(GET_APPCOMMAND_LPARAM(lParam));
    if (vk == 0) return false;
    return HandleMediaShortcutEvent(MakeKeyBinding(vk),
                                    MediaShortcutSource::AppCommand);
}

bool IsModifierVirtualKey(UINT vk) {
    switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

int ShortcutActionIndexFromControlId(int id) {
    if (id >= IDC_SHORTCUT_PRIMARY_BASE &&
        id < IDC_SHORTCUT_PRIMARY_BASE + static_cast<int>(kShortcutActionCount)) {
        return id - IDC_SHORTCUT_PRIMARY_BASE;
    }
    if (id >= IDC_SHORTCUT_ALT_BASE &&
        id < IDC_SHORTCUT_ALT_BASE + static_cast<int>(kShortcutActionCount)) {
        return id - IDC_SHORTCUT_ALT_BASE;
    }
    return -1;
}

int ShortcutSlotFromControlId(int id) {
    if (id >= IDC_SHORTCUT_PRIMARY_BASE &&
        id < IDC_SHORTCUT_PRIMARY_BASE + static_cast<int>(kShortcutActionCount)) {
        return 0;
    }
    if (id >= IDC_SHORTCUT_ALT_BASE &&
        id < IDC_SHORTCUT_ALT_BASE + static_cast<int>(kShortcutActionCount)) {
        return 1;
    }
    return -1;
}

void UpdateShortcutEdit(HWND dialog, size_t action, size_t slot) {
    const int id = (slot == 0 ? IDC_SHORTCUT_PRIMARY_BASE : IDC_SHORTCUT_ALT_BASE) +
                   static_cast<int>(action);
    SetDlgItemTextW(dialog, id,
                    KeyBindingText(g_shortcutDialogBindings[action][slot]).c_str());
}

void UpdateAllShortcutEdits(HWND dialog) {
    for (size_t action = 0; action < kShortcutActionCount; ++action) {
        for (size_t slot = 0; slot < kShortcutSlotCount; ++slot) {
            UpdateShortcutEdit(dialog, action, slot);
        }
    }
}

void AssignShortcutDialogBinding(HWND edit, const KeyBinding& binding) {
    if (!edit || binding.vk == 0) return;

    const int id = GetDlgCtrlID(edit);
    const int action = ShortcutActionIndexFromControlId(id);
    const int slot = ShortcutSlotFromControlId(id);
    if (action < 0 || slot < 0) return;

    // Keep each shortcut unambiguous. Assigning an existing shortcut moves it
    // to the newly selected action/slot instead of creating a hidden conflict.
    for (size_t a = 0; a < kShortcutActionCount; ++a) {
        for (size_t s = 0; s < kShortcutSlotCount; ++s) {
            if (static_cast<int>(a) == action && static_cast<int>(s) == slot) {
                continue;
            }
            if (g_shortcutDialogBindings[a][s].vk != 0 &&
                SameKeyBinding(g_shortcutDialogBindings[a][s], binding)) {
                g_shortcutDialogBindings[a][s] = {};
                UpdateShortcutEdit(GetParent(edit), a, s);
            }
        }
    }

    g_shortcutDialogBindings[static_cast<size_t>(action)]
                            [static_cast<size_t>(slot)] = binding;
    UpdateShortcutEdit(GetParent(edit),
                       static_cast<size_t>(action),
                       static_cast<size_t>(slot));
}

LRESULT CALLBACK ShortcutEditProc(HWND hwnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SETFOCUS:
        g_lastShortcutEdit = hwnd;
        break;

    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTARROWS;

    case WM_APPCOMMAND: {
        const UINT vk = VirtualKeyFromAppCommand(GET_APPCOMMAND_LPARAM(lParam));
        if (vk != 0) {
            AssignShortcutDialogBinding(hwnd, MakeKeyBinding(vk));
            return TRUE;
        }
        break;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        const UINT vk = static_cast<UINT>(wParam);
        if (vk == VK_TAB) {
            HWND dialog = GetParent(hwnd);
            const bool backwards = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            HWND next = GetNextDlgTabItem(dialog, hwnd, backwards);
            if (next) SetFocus(next);
            return 0;
        }
        if (IsModifierVirtualKey(vk)) return 0;
        if ((GetKeyState(VK_LWIN) & 0x8000) != 0 ||
            (GetKeyState(VK_RWIN) & 0x8000) != 0) {
            return 0;
        }

        KeyBinding binding{};
        binding.vk = vk;
        binding.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        binding.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        binding.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        AssignShortcutDialogBinding(hwnd, binding);
        return 0;
    }

    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_PASTE:
    case WM_CUT:
    case WM_CLEAR:
        return 0;
    }

    return CallWindowProcW(g_shortcutEditOriginalProc, hwnd, msg, wParam, lParam);
}

INT_PTR CALLBACK ShortcutsProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        ApplyDarkTheme(dialog);
        g_shortcutDialogBindings = g_shortcuts;
        g_lastShortcutEdit = nullptr;

        for (size_t action = 0; action < kShortcutActionCount; ++action) {
            const int primaryId =
                IDC_SHORTCUT_PRIMARY_BASE + static_cast<int>(action);
            const int alternateId =
                IDC_SHORTCUT_ALT_BASE + static_cast<int>(action);

            HWND primary = GetDlgItem(dialog, primaryId);
            HWND alternate = GetDlgItem(dialog, alternateId);

            for (HWND edit : { primary, alternate }) {
                if (!edit) continue;
                ApplyDarkTheme(edit);
                const WNDPROC previous = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrW(edit, GWLP_WNDPROC,
                                     reinterpret_cast<LONG_PTR>(ShortcutEditProc)));
                if (!g_shortcutEditOriginalProc && previous) {
                    g_shortcutEditOriginalProc = previous;
                }
            }
        }

        for (int id : { IDC_SHORTCUT_CLEAR, IDC_SHORTCUT_RESET, IDOK, IDCANCEL }) {
            HWND child = GetDlgItem(dialog, id);
            if (child) ApplyDarkTheme(child);
        }

        UpdateAllShortcutEdits(dialog);

        RECT owner{}, dlg{};
        GetWindowRect(g_main, &owner);
        GetWindowRect(dialog, &dlg);
        const int x = owner.left +
            ((owner.right - owner.left) - (dlg.right - dlg.left)) / 2;
        const int y = owner.top +
            ((owner.bottom - owner.top) - (dlg.bottom - dlg.top)) / 2;
        SetWindowPos(dialog, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

        // Start on a neutral button rather than a capture field. That keeps
        // ordinary Enter/Esc dialog behavior until the user deliberately
        // clicks a shortcut box.
        HWND okButton = GetDlgItem(dialog, IDOK);
        if (okButton) SetFocus(okButton);
        return FALSE;
    }

    case WM_APPCOMMAND: {
        const UINT vk = VirtualKeyFromAppCommand(GET_APPCOMMAND_LPARAM(lParam));
        if (vk != 0) {
            HWND focus = GetFocus();
            if (focus && GetParent(focus) == dialog &&
                ShortcutActionIndexFromControlId(GetDlgCtrlID(focus)) >= 0) {
                AssignShortcutDialogBinding(focus, MakeKeyBinding(vk));
                return TRUE;
            }
        }
        break;
    }

    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(g_panelBrush);

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_PANEL);
        return reinterpret_cast<INT_PTR>(g_panelBrush);
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, RGB(28, 28, 28));
        return reinterpret_cast<INT_PTR>(g_editBrush ? g_editBrush : g_panelBrush);
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_SHORTCUT_CLEAR: {
            HWND edit = g_lastShortcutEdit;
            if (edit && IsWindow(edit) && GetParent(edit) == dialog) {
                const int id = GetDlgCtrlID(edit);
                const int action = ShortcutActionIndexFromControlId(id);
                const int slot = ShortcutSlotFromControlId(id);
                if (action >= 0 && slot >= 0) {
                    g_shortcutDialogBindings[static_cast<size_t>(action)]
                                            [static_cast<size_t>(slot)] = {};
                    UpdateShortcutEdit(dialog,
                                       static_cast<size_t>(action),
                                       static_cast<size_t>(slot));
                    SetFocus(edit);
                }
            }
            return TRUE;
        }

        case IDC_SHORTCUT_RESET:
            g_shortcutDialogBindings = DefaultShortcutBindings();
            UpdateAllShortcutEdits(dialog);
            return TRUE;

        case IDOK:
            g_shortcuts = g_shortcutDialogBindings;
            SaveShortcutSettings();
            RefreshMenuShortcutLabels();
            g_lastShortcutEdit = nullptr;
            EndDialog(dialog, IDOK);
            return TRUE;

        case IDCANCEL:
            g_lastShortcutEdit = nullptr;
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }

    return FALSE;
}


INT_PTR CALLBACK OptionsProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        ApplyDarkTheme(dialog);

        const int ids[] = {
            IDC_OPT_DELAY, IDC_OPT_OSD_SIZE, IDC_OPT_OSD, IDC_OPT_PLAYLIST_START_OSD,
            IDC_OPT_GPU_API, IDC_OPT_SUB_SIZE, IDC_OPT_AUDIO_LANG, IDC_OPT_SUB_LANG,
            IDC_OPT_EXIT_FULLSCREEN_END, IDC_OPT_INSTANCE_BEHAVIOR,
            IDC_OPT_HIDE_WINDOWED_CURSOR, IDC_OPT_TASKBAR_BUTTONS,
            IDC_OPT_SHOW_STOP_BUTTON, IDC_OPT_SHOW_PLAYLIST_BUTTONS,
            IDC_OPT_SCREENSHOT_FOLDER, IDC_OPT_SCREENSHOT_BROWSE,
            IDC_OPT_SHORTCUTS, IDOK, IDCANCEL
        };
        for (int id : ids) {
            HWND child = GetDlgItem(dialog, id);
            if (child) ApplyDarkTheme(child);
        }

        SetDlgItemInt(dialog, IDC_OPT_DELAY, g_singleClickDelayMs, FALSE);
        SetDlgItemInt(dialog, IDC_OPT_OSD_SIZE, g_osdFontSize, FALSE);
        SetDlgItemInt(dialog, IDC_OPT_SUB_SIZE, g_subtitleFontSize, FALSE);

        HWND combo = GetDlgItem(dialog, IDC_OPT_OSD);
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Center - golden ratio"));
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Top left"));
        SendMessageW(combo, CB_SETCURSEL,
                     g_osdPosition == OsdPosition::TopLeft ? 1 : 0, 0);
        SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 20);
        SendMessageW(combo, CB_SETITEMHEIGHT, 0, 20);

        HWND playlistStartCombo = GetDlgItem(dialog, IDC_OPT_PLAYLIST_START_OSD);
        SendMessageW(playlistStartCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Nothing"));
        SendMessageW(playlistStartCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Show title"));
        SendMessageW(playlistStartCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Show playlist"));
        SendMessageW(playlistStartCombo, CB_SETCURSEL,
                     static_cast<WPARAM>(g_playlistStartOsd), 0);
        SendMessageW(playlistStartCombo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 20);
        SendMessageW(playlistStartCombo, CB_SETITEMHEIGHT, 0, 20);

        HWND gpuApiCombo = GetDlgItem(dialog, IDC_OPT_GPU_API);
        SendMessageW(gpuApiCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Auto (recommended)"));
        SendMessageW(gpuApiCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Direct3D 11"));
        SendMessageW(gpuApiCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Vulkan"));
        SendMessageW(gpuApiCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"OpenGL"));
        SendMessageW(gpuApiCombo, CB_SETCURSEL,
                     static_cast<WPARAM>(g_gpuApi), 0);
        SendMessageW(gpuApiCombo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 20);
        SendMessageW(gpuApiCombo, CB_SETITEMHEIGHT, 0, 20);

        SetDlgItemTextW(dialog, IDC_OPT_AUDIO_LANG,
                        g_preferredAudioLanguage.c_str());
        SetDlgItemTextW(dialog, IDC_OPT_SUB_LANG,
                        g_preferredSubtitleLanguage.c_str());
        CheckDlgButton(dialog, IDC_OPT_EXIT_FULLSCREEN_END,
                       g_exitFullscreenOnPlaybackEnd ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_OPT_HIDE_WINDOWED_CURSOR,
                       g_hideWindowedCursor ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_OPT_SHOW_STOP_BUTTON,
                       g_showStopButton ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_OPT_SHOW_PLAYLIST_BUTTONS,
                       g_showPlaylistButtons ? BST_CHECKED : BST_UNCHECKED);

        HWND instanceCombo = GetDlgItem(dialog, IDC_OPT_INSTANCE_BEHAVIOR);
        SendMessageW(instanceCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Open the new file in the existing instance"));
        SendMessageW(instanceCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Pause existing instance and open a new instance"));
        SendMessageW(instanceCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Just open a new instance"));
        SendMessageW(instanceCombo, CB_SETCURSEL,
                     static_cast<WPARAM>(g_instanceOpenBehavior), 0);
        SendMessageW(instanceCombo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 20);
        SendMessageW(instanceCombo, CB_SETITEMHEIGHT, 0, 20);

        HWND taskbarCombo = GetDlgItem(dialog, IDC_OPT_TASKBAR_BUTTONS);
        SendMessageW(taskbarCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Play/Pause + Fullscreen"));
        SendMessageW(taskbarCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Playlist Previous/Next"));
        SendMessageW(taskbarCombo, CB_SETCURSEL,
                     g_taskbarPlaylistControls ? 1 : 0, 0);
        SendMessageW(taskbarCombo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 20);
        SendMessageW(taskbarCombo, CB_SETITEMHEIGHT, 0, 20);
        SendMessageW(taskbarCombo, CB_SETDROPPEDWIDTH, 190, 0);

        SetDlgItemTextW(dialog, IDC_OPT_SCREENSHOT_FOLDER,
                        ScreenshotDirectory().wstring().c_str());

        RECT owner{}, dlg{};
        GetWindowRect(g_main, &owner);
        GetWindowRect(dialog, &dlg);
        const int x = owner.left +
            ((owner.right - owner.left) - (dlg.right - dlg.left)) / 2;
        const int y = owner.top +
            ((owner.bottom - owner.top) - (dlg.bottom - dlg.top)) / 2;
        SetWindowPos(dialog, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    }

    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(g_panelBrush);

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_PANEL);
        return reinterpret_cast<INT_PTR>(g_panelBrush);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, RGB(28, 28, 28));
        return reinterpret_cast<INT_PTR>(g_editBrush ? g_editBrush : g_panelBrush);
    }

    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (measure && (measure->CtlID == IDC_OPT_OSD ||
                        measure->CtlID == IDC_OPT_PLAYLIST_START_OSD ||
                        measure->CtlID == IDC_OPT_GPU_API ||
                        measure->CtlID == IDC_OPT_INSTANCE_BEHAVIOR ||
                        measure->CtlID == IDC_OPT_TASKBAR_BUTTONS)) {
            measure->itemHeight = 20;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw && (draw->CtlID == IDC_OPT_OSD ||
                     draw->CtlID == IDC_OPT_PLAYLIST_START_OSD ||
                     draw->CtlID == IDC_OPT_GPU_API ||
                     draw->CtlID == IDC_OPT_INSTANCE_BEHAVIOR ||
                     draw->CtlID == IDC_OPT_TASKBAR_BUTTONS)) {
            const bool selected = (draw->itemState & ODS_SELECTED) != 0;
            const COLORREF background = selected ? RGB(48, 48, 48) : RGB(28, 28, 28);

            HBRUSH brush = CreateSolidBrush(background);
            FillRect(draw->hDC, &draw->rcItem, brush);
            DeleteObject(brush);

            wchar_t text[128]{};
            HWND combo = GetDlgItem(dialog, static_cast<int>(draw->CtlID));
            int item = static_cast<int>(draw->itemID);
            if (item < 0) {
                item = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
            }
            if (item >= 0) {
                SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(item),
                             reinterpret_cast<LPARAM>(text));
            }

            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, C_TEXT);

            HFONT oldFont = nullptr;
            if (g_font) {
                oldFont = static_cast<HFONT>(
                    SelectObject(draw->hDC, g_font));
            }

            RECT textRect = draw->rcItem;
            textRect.left += 7;
            textRect.right -= 4;
            DrawTextW(draw->hDC, text, -1, &textRect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            if (oldFont) {
                SelectObject(draw->hDC, oldFont);
            }

            if ((draw->itemState & ODS_FOCUS) != 0) {
                RECT focus = draw->rcItem;
                InflateRect(&focus, -1, -1);
                DrawFocusRect(draw->hDC, &focus);
            }
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_OPT_SCREENSHOT_BROWSE: {
            wchar_t current[32768]{};
            GetDlgItemTextW(dialog, IDC_OPT_SCREENSHOT_FOLDER, current,
                            static_cast<int>(std::size(current)));
            std::wstring selected = current;
            if (BrowseForScreenshotFolder(dialog, selected)) {
                SetDlgItemTextW(dialog, IDC_OPT_SCREENSHOT_FOLDER, selected.c_str());
            }
            return TRUE;
        }

        case IDC_OPT_SHORTCUTS:
            DialogBoxParamW(g_instance, MAKEINTRESOURCEW(203), dialog,
                            ShortcutsProc, 0);
            return TRUE;

        case IDOK: {
            BOOL ok = FALSE;
            const UINT delay = GetDlgItemInt(dialog, IDC_OPT_DELAY, &ok, FALSE);
            if (ok) {
                g_singleClickDelayMs =
                    static_cast<UINT>(std::clamp<int>(static_cast<int>(delay), 20, 500));
            }

            BOOL sizeOk = FALSE;
            const UINT osdSize = GetDlgItemInt(dialog, IDC_OPT_OSD_SIZE, &sizeOk, FALSE);
            if (sizeOk) {
                g_osdFontSize = std::clamp<int>(static_cast<int>(osdSize), 24, 180);
            }

            BOOL subtitleSizeOk = FALSE;
            const UINT subtitleSize =
                GetDlgItemInt(dialog, IDC_OPT_SUB_SIZE, &subtitleSizeOk, FALSE);
            if (subtitleSizeOk) {
                g_subtitleFontSize =
                    std::clamp<int>(static_cast<int>(subtitleSize), 20, 120);
            }

            HWND combo = GetDlgItem(dialog, IDC_OPT_OSD);
            const int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
            g_osdPosition = sel == 1 ? OsdPosition::TopLeft : OsdPosition::GoldenCenter;

            HWND playlistStartCombo = GetDlgItem(dialog, IDC_OPT_PLAYLIST_START_OSD);
            const int playlistStartSel = static_cast<int>(
                SendMessageW(playlistStartCombo, CB_GETCURSEL, 0, 0));
            g_playlistStartOsd = static_cast<PlaylistStartOsd>(
                std::clamp(playlistStartSel, 0, 2));

            HWND gpuApiCombo = GetDlgItem(dialog, IDC_OPT_GPU_API);
            const int gpuApiSel = static_cast<int>(
                SendMessageW(gpuApiCombo, CB_GETCURSEL, 0, 0));
            g_gpuApi = static_cast<GpuApi>(std::clamp(gpuApiSel, 0, 3));

            wchar_t audio[128]{};
            wchar_t sub[128]{};
            GetDlgItemTextW(dialog, IDC_OPT_AUDIO_LANG, audio,
                            static_cast<int>(std::size(audio)));
            GetDlgItemTextW(dialog, IDC_OPT_SUB_LANG, sub,
                            static_cast<int>(std::size(sub)));

            g_preferredAudioLanguage = audio;
            g_preferredSubtitleLanguage = sub;
            g_exitFullscreenOnPlaybackEnd =
                IsDlgButtonChecked(dialog, IDC_OPT_EXIT_FULLSCREEN_END) == BST_CHECKED;
            g_hideWindowedCursor =
                IsDlgButtonChecked(dialog, IDC_OPT_HIDE_WINDOWED_CURSOR) == BST_CHECKED;
            g_showStopButton =
                IsDlgButtonChecked(dialog, IDC_OPT_SHOW_STOP_BUTTON) == BST_CHECKED;
            g_showPlaylistButtons =
                IsDlgButtonChecked(dialog, IDC_OPT_SHOW_PLAYLIST_BUTTONS) == BST_CHECKED;

            HWND instanceCombo = GetDlgItem(dialog, IDC_OPT_INSTANCE_BEHAVIOR);
            const int instanceSel = static_cast<int>(
                SendMessageW(instanceCombo, CB_GETCURSEL, 0, 0));
            g_instanceOpenBehavior = static_cast<InstanceOpenBehavior>(
                std::clamp(instanceSel, 0, 2));

            HWND taskbarCombo = GetDlgItem(dialog, IDC_OPT_TASKBAR_BUTTONS);
            const int taskbarSel = static_cast<int>(
                SendMessageW(taskbarCombo, CB_GETCURSEL, 0, 0));
            g_taskbarPlaylistControls = taskbarSel == 1;

            wchar_t screenshotFolder[32768]{};
            GetDlgItemTextW(dialog, IDC_OPT_SCREENSHOT_FOLDER, screenshotFolder,
                            static_cast<int>(std::size(screenshotFolder)));
            std::wstring requestedScreenshotFolder = screenshotFolder;
            while (!requestedScreenshotFolder.empty() &&
                   (requestedScreenshotFolder.back() == L' ' ||
                    requestedScreenshotFolder.back() == L'\\' ||
                    requestedScreenshotFolder.back() == L'/')) {
                // Preserve a drive root such as C:\ while trimming decorative
                // trailing separators from normal folder paths.
                if (requestedScreenshotFolder.size() == 3 &&
                    requestedScreenshotFolder[1] == L':' &&
                    (requestedScreenshotFolder[2] == L'\\' ||
                     requestedScreenshotFolder[2] == L'/')) break;
                requestedScreenshotFolder.pop_back();
            }
            const std::filesystem::path defaultScreenshotFolder =
                DefaultScreenshotDirectory();
            if (requestedScreenshotFolder.empty() ||
                _wcsicmp(requestedScreenshotFolder.c_str(),
                         defaultScreenshotFolder.wstring().c_str()) == 0) {
                g_screenshotDirectory.clear();
            } else {
                g_screenshotDirectory = requestedScreenshotFolder;
            }

            if (!g_hideWindowedCursor) {
                SetWindowedCursorHidden(false);
            }

            LayoutChildren(g_main);

            if (g_mpvReady) {
                if (!g_preferredAudioLanguage.empty()) {
                    MpvSetString("alang", g_preferredAudioLanguage);
                }
                if (!g_preferredSubtitleLanguage.empty()) {
                    MpvSetString("slang", g_preferredSubtitleLanguage);
                }
                MpvCommand({"set", "osd-font-size", std::to_string(g_osdFontSize)});
                MpvCommand({"set", "sub-font-size", std::to_string(g_subtitleFontSize)});
                if (g_playlistOsdVisible) ShowPlaylistOsd();
                else UpdateOsdPosition();
            }

            const bool idle = !g_mpvReady || MpvGetFlag("idle-active", true);
            const bool paused = !idle && MpvGetFlag("pause", true);
            UpdateTaskbarButtons(idle, paused, true);

            SaveSettings();
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}



struct CutterInputDialogState {
    std::vector<std::wstring> paths;
    bool dirty = false;
};

void RefreshCutterInputList(HWND dialog, CutterInputDialogState& state, int selectIndex = -1) {
    HWND list = GetDlgItem(dialog, IDC_INPUT_LIST);
    if (!list) return;

    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < state.paths.size(); ++i) {
        const std::wstring name = std::filesystem::path(state.paths[i]).filename().wstring();
        const std::wstring text = std::to_wstring(i + 1) + L".  " +
            (name.empty() ? state.paths[i] : name);
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);

    if (!state.paths.empty()) {
        if (selectIndex < 0) selectIndex = 0;
        selectIndex = std::clamp(selectIndex, 0, static_cast<int>(state.paths.size()) - 1);
        SendMessageW(list, LB_SETCURSEL, selectIndex, 0);
    }
}

INT_PTR CALLBACK CutterInputsProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<CutterInputDialogState*>(
        GetWindowLongPtrW(dialog, DWLP_USER));

    switch (msg) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<CutterInputDialogState*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        ApplyDarkTheme(dialog);
        for (int id : { IDC_INPUT_LIST, IDC_INPUT_ADD, IDC_INPUT_REMOVE,
                        IDC_INPUT_UP, IDC_INPUT_DOWN, IDC_INPUT_SORT,
                        IDC_INPUT_CLEAR, IDOK, IDCANCEL }) {
            if (HWND child = GetDlgItem(dialog, id)) ApplyDarkTheme(child);
        }
        if (HWND list = GetDlgItem(dialog, IDC_INPUT_LIST)) {
            SetWindowTheme(list, L"DarkMode_Explorer", nullptr);
        }
        if (state) RefreshCutterInputList(dialog, *state, 0);

        RECT owner{}, dlg{};
        GetWindowRect(g_main, &owner);
        GetWindowRect(dialog, &dlg);
        const int x = owner.left + ((owner.right - owner.left) - (dlg.right - dlg.left)) / 2;
        const int y = owner.top + ((owner.bottom - owner.top) - (dlg.bottom - dlg.top)) / 2;
        SetWindowPos(dialog, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    }

    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(g_panelBrush);

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, C_PANEL);
        SetTextColor(dc, C_TEXT);
        return reinterpret_cast<INT_PTR>(g_panelBrush);
    }

    case WM_COMMAND: {
        if (!state) break;
        const int id = LOWORD(wParam);
        HWND list = GetDlgItem(dialog, IDC_INPUT_LIST);
        const int selected = list
            ? static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0))
            : LB_ERR;

        if (id == IDC_INPUT_ADD) {
            const auto added = ChooseMediaFiles(dialog);
            if (!added.empty()) {
                const int firstNew = static_cast<int>(state->paths.size());
                for (const auto& path : added) {
                    if (!path.empty()) state->paths.push_back(AbsoluteLocalPath(path));
                }
                state->dirty = true;
                RefreshCutterInputList(dialog, *state, firstNew);
            }
            return TRUE;
        }
        if (id == IDC_INPUT_REMOVE) {
            if (selected != LB_ERR && selected >= 0 &&
                selected < static_cast<int>(state->paths.size())) {
                state->paths.erase(state->paths.begin() + selected);
                state->dirty = true;
                RefreshCutterInputList(dialog, *state,
                    std::min(selected, static_cast<int>(state->paths.size()) - 1));
            }
            return TRUE;
        }
        if (id == IDC_INPUT_UP) {
            if (selected > 0 && selected < static_cast<int>(state->paths.size())) {
                std::swap(state->paths[selected], state->paths[selected - 1]);
                state->dirty = true;
                RefreshCutterInputList(dialog, *state, selected - 1);
            }
            return TRUE;
        }
        if (id == IDC_INPUT_DOWN) {
            if (selected != LB_ERR && selected >= 0 &&
                selected + 1 < static_cast<int>(state->paths.size())) {
                std::swap(state->paths[selected], state->paths[selected + 1]);
                state->dirty = true;
                RefreshCutterInputList(dialog, *state, selected + 1);
            }
            return TRUE;
        }
        if (id == IDC_INPUT_SORT) {
            std::stable_sort(state->paths.begin(), state->paths.end(),
                [](const std::wstring& a, const std::wstring& b) {
                    const std::wstring an = std::filesystem::path(a).filename().wstring();
                    const std::wstring bn = std::filesystem::path(b).filename().wstring();
                    const int logical = StrCmpLogicalW(an.c_str(), bn.c_str());
                    if (logical != 0) return logical < 0;
                    return _wcsicmp(a.c_str(), b.c_str()) < 0;
                });
            state->dirty = true;
            RefreshCutterInputList(dialog, *state, 0);
            return TRUE;
        }
        if (id == IDC_INPUT_CLEAR) {
            if (!state->paths.empty()) {
                state->paths.clear();
                state->dirty = true;
                RefreshCutterInputList(dialog, *state, -1);
            }
            return TRUE;
        }
        if (id == IDOK) {
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

void ShowCutterInputManager() {
    CutterInputDialogState state;
    state.paths = g_cutInputs;
    const INT_PTR result = DialogBoxParamW(
        g_instance, MAKEINTRESOURCEW(205), g_main,
        CutterInputsProc, reinterpret_cast<LPARAM>(&state));
    if (result != IDOK || !state.dirty) return;

    if (state.paths.empty()) {
        ClearCutterInputs();
    } else {
        LoadPlaylist(state.paths);
    }
}

double CutterProbeExternalAudioDurationSeconds(const std::wstring& rawPath) {
    const std::wstring path = AbsoluteLocalPath(rawPath);
    if (path.empty() || !FileExists(path)) return -1.0;

    const std::wstring ffprobe = FindCutterTool(L"ffprobe.exe");
    if (ffprobe.empty()) return -1.0;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return -1.0;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nullHandle = CreateFileW(
        L"NUL",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (nullHandle == INVALID_HANDLE_VALUE) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return -1.0;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nullHandle;
    si.hStdOutput = writePipe;
    si.hStdError = nullHandle;

    PROCESS_INFORMATION pi{};
    std::wstring command =
        L"\"" + ffprobe + L"\" -v error -show_entries format=duration "
        L"-of default=nw=1:nk=1 \"" + path + L"\"";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    const BOOL started = CreateProcessW(
        ffprobe.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    CloseHandle(writePipe);
    CloseHandle(nullHandle);
    if (!started) {
        CloseHandle(readPipe);
        return -1.0;
    }

    std::string output;
    char buffer[256]{};
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        output.append(buffer, buffer + read);
    }
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    char* end = nullptr;
    const double duration = std::strtod(output.c_str(), &end);
    if (!end || end == output.c_str() || !std::isfinite(duration) || duration <= 0.0) {
        return -1.0;
    }
    return duration;
}

struct CutterAudioTrackDialogState {
    std::vector<CutterManagedAudioTrack> tracks;
    bool dirty = false;
};

std::wstring CutterAudioTrackRowText(const CutterManagedAudioTrack& track,
                                     size_t row) {
    std::wstring text = std::to_wstring(row + 1) + L".  ";
    text += track.included ? L"[IN]  " : L"[OUT] ";
    if (track.embedded) {
        text += L"Embedded audio " + std::to_wstring(track.streamIndex + 1);
    } else {
        text += L"External: ";
        const std::wstring name =
            std::filesystem::path(track.externalPath).filename().wstring();
        text += name.empty() ? track.externalPath : name;
    }

    const std::wstring details =
        CutterManagedAudioDetails(track.language, track.title);
    if (!details.empty()) text += L"  |  " + details;
    if (track.durationSeconds > 0.0) {
        text += L"  |  Length " + FormatTime(track.durationSeconds);
    } else {
        text += L"  |  Length --:--";
    }
    if (track.offsetMs != 0) {
        text += L"  |  Offset ";
        if (track.offsetMs > 0) text += L"+";
        text += std::to_wstring(track.offsetMs) + L" ms";
    }
    return text;
}

void RefreshCutterAudioTrackList(HWND dialog,
                                 CutterAudioTrackDialogState& state,
                                 int selectIndex = -1) {
    HWND list = GetDlgItem(dialog, IDC_AUDIO_TRACK_LIST);
    if (!list) return;

    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < state.tracks.size(); ++i) {
        const std::wstring rowText =
            CutterAudioTrackRowText(state.tracks[i], i);
        SendMessageW(list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(rowText.c_str()));
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);

    if (!state.tracks.empty()) {
        if (selectIndex < 0) selectIndex = 0;
        selectIndex = std::clamp(
            selectIndex, 0, static_cast<int>(state.tracks.size()) - 1);
        SendMessageW(list, LB_SETCURSEL, selectIndex, 0);
    }
}

void UpdateCutterAudioTrackDialogButtons(
    HWND dialog,
    const CutterAudioTrackDialogState& state) {

    HWND list = GetDlgItem(dialog, IDC_AUDIO_TRACK_LIST);
    const int selected = list
        ? static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0))
        : LB_ERR;
    const bool valid =
        selected >= 0 && selected < static_cast<int>(state.tracks.size());
    const bool external =
        valid && !state.tracks[static_cast<size_t>(selected)].embedded;

    if (HWND h = GetDlgItem(dialog, IDC_AUDIO_TRACK_TOGGLE))
        EnableWindow(h, valid);
    if (HWND h = GetDlgItem(dialog, IDC_AUDIO_TRACK_REMOVE))
        EnableWindow(h, external);
    if (HWND h = GetDlgItem(dialog, IDC_AUDIO_TRACK_UP))
        EnableWindow(h, valid && selected > 0);
    if (HWND h = GetDlgItem(dialog, IDC_AUDIO_TRACK_DOWN)) {
        EnableWindow(
            h,
            valid &&
            selected + 1 < static_cast<int>(state.tracks.size()));
    }
}

INT_PTR CALLBACK CutterAudioTracksProc(
    HWND dialog,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam) {

    auto* state = reinterpret_cast<CutterAudioTrackDialogState*>(
        GetWindowLongPtrW(dialog, DWLP_USER));

    switch (msg) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<CutterAudioTrackDialogState*>(lParam);
        SetWindowLongPtrW(
            dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));

        ApplyDarkTheme(dialog);
        for (int id : {
                 IDC_AUDIO_TRACK_LIST,
                 IDC_AUDIO_TRACK_ADD,
                 IDC_AUDIO_TRACK_TOGGLE,
                 IDC_AUDIO_TRACK_REMOVE,
                 IDC_AUDIO_TRACK_UP,
                 IDC_AUDIO_TRACK_DOWN,
                 IDOK,
                 IDCANCEL }) {
            if (HWND child = GetDlgItem(dialog, id)) {
                ApplyDarkTheme(child);
            }
        }

        if (HWND list = GetDlgItem(dialog, IDC_AUDIO_TRACK_LIST)) {
            SetWindowTheme(list, L"DarkMode_Explorer", nullptr);
        }

        if (state) {
            RefreshCutterAudioTrackList(dialog, *state, 0);
            UpdateCutterAudioTrackDialogButtons(dialog, *state);
        }

        RECT owner{}, dlg{};
        GetWindowRect(g_main, &owner);
        GetWindowRect(dialog, &dlg);
        const int x =
            owner.left +
            ((owner.right - owner.left) - (dlg.right - dlg.left)) / 2;
        const int y =
            owner.top +
            ((owner.bottom - owner.top) - (dlg.bottom - dlg.top)) / 2;
        SetWindowPos(
            dialog, nullptr, x, y, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    }

    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(g_panelBrush);

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, C_PANEL);
        SetTextColor(dc, C_TEXT);
        return reinterpret_cast<INT_PTR>(g_panelBrush);
    }

    case WM_COMMAND: {
        if (!state) break;

        const int id = LOWORD(wParam);
        HWND list = GetDlgItem(dialog, IDC_AUDIO_TRACK_LIST);
        const int selected = list
            ? static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0))
            : LB_ERR;

        if (id == IDC_AUDIO_TRACK_LIST &&
            HIWORD(wParam) == LBN_SELCHANGE) {
            UpdateCutterAudioTrackDialogButtons(dialog, *state);
            return TRUE;
        }

        if (id == IDC_AUDIO_TRACK_ADD) {
            const auto paths = ChooseAudioFiles(dialog);
            if (!paths.empty()) {
                int firstNew = static_cast<int>(state->tracks.size());
                for (const auto& path : paths) {
                    if (path.empty()) continue;

                    const std::wstring absolute =
                        AbsoluteLocalPath(path);
                    const bool duplicate = std::any_of(
                        state->tracks.begin(),
                        state->tracks.end(),
                        [&](const CutterManagedAudioTrack& track) {
                            return !track.embedded &&
                                _wcsicmp(
                                    AbsoluteLocalPath(
                                        track.externalPath).c_str(),
                                    absolute.c_str()) == 0;
                        });
                    if (duplicate) continue;

                    CutterManagedAudioTrack track{};
                    track.embedded = false;
                    track.externalPath = absolute;
                    track.included = true;
                    track.durationSeconds =
                        CutterProbeExternalAudioDurationSeconds(absolute);
                    state->tracks.push_back(std::move(track));
                }

                state->dirty = true;
                RefreshCutterAudioTrackList(
                    dialog, *state, firstNew);
                UpdateCutterAudioTrackDialogButtons(
                    dialog, *state);
            }
            return TRUE;
        }

        if (id == IDC_AUDIO_TRACK_TOGGLE) {
            if (selected >= 0 &&
                selected < static_cast<int>(state->tracks.size())) {
                auto& track =
                    state->tracks[static_cast<size_t>(selected)];
                track.included = !track.included;
                state->dirty = true;
                RefreshCutterAudioTrackList(
                    dialog, *state, selected);
                UpdateCutterAudioTrackDialogButtons(
                    dialog, *state);
            }
            return TRUE;
        }

        if (id == IDC_AUDIO_TRACK_REMOVE) {
            if (selected >= 0 &&
                selected < static_cast<int>(state->tracks.size()) &&
                !state->tracks[
                    static_cast<size_t>(selected)].embedded) {
                state->tracks.erase(
                    state->tracks.begin() + selected);
                state->dirty = true;
                RefreshCutterAudioTrackList(
                    dialog,
                    *state,
                    std::min(
                        selected,
                        static_cast<int>(
                            state->tracks.size()) - 1));
                UpdateCutterAudioTrackDialogButtons(
                    dialog, *state);
            }
            return TRUE;
        }

        if (id == IDC_AUDIO_TRACK_UP) {
            if (selected > 0 &&
                selected < static_cast<int>(state->tracks.size())) {
                std::swap(
                    state->tracks[static_cast<size_t>(selected)],
                    state->tracks[
                        static_cast<size_t>(selected - 1)]);
                state->dirty = true;
                RefreshCutterAudioTrackList(
                    dialog, *state, selected - 1);
                UpdateCutterAudioTrackDialogButtons(
                    dialog, *state);
            }
            return TRUE;
        }

        if (id == IDC_AUDIO_TRACK_DOWN) {
            if (selected >= 0 &&
                selected + 1 <
                    static_cast<int>(state->tracks.size())) {
                std::swap(
                    state->tracks[static_cast<size_t>(selected)],
                    state->tracks[
                        static_cast<size_t>(selected + 1)]);
                state->dirty = true;
                RefreshCutterAudioTrackList(
                    dialog, *state, selected + 1);
                UpdateCutterAudioTrackDialogButtons(
                    dialog, *state);
            }
            return TRUE;
        }

        if (id == IDOK) {
            EndDialog(dialog, IDOK);
            return TRUE;
        }

        if (id == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }

    return FALSE;
}

void ShowCutterAudioTrackManager() {
    if (g_cutInputs.empty() && g_currentMediaPath.empty()) {
        MessageBoxW(
            g_main,
            L"Open an input file first.",
            kAppTitle,
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (g_cutInputs.size() > 1) {
        MessageBoxW(
            g_main,
            L"Audio track management is currently limited to single-file jobs.\n\n"
            L"MULTI joins need a separate ffprobe-based track-matching pass.",
            kAppTitle,
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (g_cutAudioTracksInitialized) {
        CutterCommitAudioSyncOffsetEdit(false);
    }

    const bool wasInitialized = g_cutAudioTracksInitialized;
    const auto previousTracks = g_cutAudioTracks;
    const int previousSyncIndex = g_cutAudioSyncTrackIndex;

    bool activeWasEmbedded = false;
    int activeEmbeddedStreamIndex = -1;
    std::wstring activeExternalPath;
    if (const auto* active = ActiveManagedAudioSyncTrackConst()) {
        activeWasEmbedded = active->embedded;
        if (active->embedded) {
            activeEmbeddedStreamIndex = active->streamIndex;
        } else {
            activeExternalPath = AbsoluteLocalPath(active->externalPath);
        }
    }

    EnsureCutterAudioTracksInitialized();

    CutterAudioTrackDialogState state;
    state.tracks = g_cutAudioTracks;

    const INT_PTR result = DialogBoxParamW(
        g_instance,
        MAKEINTRESOURCEW(207),
        g_main,
        CutterAudioTracksProc,
        reinterpret_cast<LPARAM>(&state));

    if (result != IDOK) {
        if (!wasInitialized) {
            g_cutAudioTracks = previousTracks;
            g_cutAudioTracksInitialized = false;
            g_cutAudioSyncTrackIndex = previousSyncIndex;
        }
        if (g_cutAudioSyncOpen) {
            SyncCutterAudioContextControls();
        }
        return;
    }

    g_cutAudioTracks = std::move(state.tracks);
    g_cutAudioTracksInitialized = true;

    g_cutAudioSyncTrackIndex = -1;
    for (size_t i = 0; i < g_cutAudioTracks.size(); ++i) {
        const auto& track = g_cutAudioTracks[i];
        if (!track.included) continue;
        const bool sameEmbedded =
            activeWasEmbedded &&
            track.embedded &&
            track.streamIndex == activeEmbeddedStreamIndex;
        const bool sameExternal =
            !activeWasEmbedded &&
            !activeExternalPath.empty() &&
            !track.embedded &&
            _wcsicmp(
                AbsoluteLocalPath(track.externalPath).c_str(),
                activeExternalPath.c_str()) == 0;
        if (sameEmbedded || sameExternal) {
            g_cutAudioSyncTrackIndex = static_cast<int>(i);
            break;
        }
    }
    if (g_cutAudioSyncTrackIndex < 0) {
        for (size_t i = 0; i < g_cutAudioTracks.size(); ++i) {
            if (g_cutAudioTracks[i].included) {
                g_cutAudioSyncTrackIndex = static_cast<int>(i);
                break;
            }
        }
    }
    if (g_cutTracks) {
        InvalidateRect(g_cutTracks, nullptr, FALSE);
    }
    if (g_cutAudioSyncOpen) {
        SyncCutterAudioContextControls();
    }

    const size_t included =
        static_cast<size_t>(std::count_if(
            g_cutAudioTracks.begin(),
            g_cutAudioTracks.end(),
            [](const CutterManagedAudioTrack& track) {
                return track.included;
            }));

    ShowOsdText(
        L"Output audio tracks: " +
            std::to_wstring(included),
        1500);
}

struct CutterSubtitleTrackDialogState {
    std::vector<CutterManagedSubtitleTrack> tracks;
    int selectedIndex = -1;
};

const wchar_t* CutterManagedSubtitleModeText(
    const CutterManagedSubtitleTrack& track) {
    return track.mode == CutterSubtitleMode::Burn ? L"BURN" : L"SOFT";
}

std::wstring CutterSubtitleTrackRowText(
    const CutterManagedSubtitleTrack& track,
    size_t row) {

    std::wstring text = std::to_wstring(row + 1) +
        (track.included ? L".  [IN]  [" : L".  [OUT] [");
    text += CutterManagedSubtitleModeText(track);
    text += L"]  ";

    if (track.embedded) {
        text += L"Embedded subtitle " +
                std::to_wstring(track.streamIndex + 1);
    } else {
        text += L"External: ";
        const std::wstring name =
            std::filesystem::path(track.externalPath).filename().wstring();
        text += name.empty() ? track.externalPath : name;
    }

    const std::wstring details =
        CutterManagedSubtitleDetails(track.language, track.title);
    if (!details.empty()) text += L"  |  " + details;
    return text;
}

void CommitCutterSubtitleDialogTitle(
    HWND dialog,
    CutterSubtitleTrackDialogState& state) {

    if (state.selectedIndex < 0 ||
        state.selectedIndex >= static_cast<int>(state.tracks.size())) {
        return;
    }

    wchar_t title[512]{};
    GetDlgItemTextW(
        dialog,
        IDC_SUB_TRACK_TITLE,
        title,
        static_cast<int>(std::size(title)));
    state.tracks[static_cast<size_t>(state.selectedIndex)].title = title;
}

void RefreshCutterSubtitleTrackList(
    HWND dialog,
    CutterSubtitleTrackDialogState& state,
    int selectIndex = -1) {

    HWND list = GetDlgItem(dialog, IDC_SUB_TRACK_LIST);
    if (!list) return;

    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < state.tracks.size(); ++i) {
        const std::wstring text =
            CutterSubtitleTrackRowText(state.tracks[i], i);
        SendMessageW(
            list,
            LB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(text.c_str()));
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);

    if (state.tracks.empty()) {
        state.selectedIndex = -1;
        return;
    }

    if (selectIndex < 0) selectIndex = state.selectedIndex;
    if (selectIndex < 0) selectIndex = 0;
    selectIndex = std::clamp(
        selectIndex, 0, static_cast<int>(state.tracks.size()) - 1);
    state.selectedIndex = selectIndex;
    SendMessageW(list, LB_SETCURSEL, selectIndex, 0);
}

void UpdateCutterSubtitleDialogControls(
    HWND dialog,
    CutterSubtitleTrackDialogState& state) {

    const bool valid =
        state.selectedIndex >= 0 &&
        state.selectedIndex < static_cast<int>(state.tracks.size());
    const CutterManagedSubtitleTrack* track =
        valid ? &state.tracks[static_cast<size_t>(state.selectedIndex)] : nullptr;
    const bool external = track && !track->embedded;

    if (HWND h = GetDlgItem(dialog, IDC_SUB_TRACK_REMOVE))
        EnableWindow(h, external ? TRUE : FALSE);
    if (HWND h = GetDlgItem(dialog, IDC_SUB_TRACK_UP))
        EnableWindow(h, valid && state.selectedIndex > 0 ? TRUE : FALSE);
    if (HWND h = GetDlgItem(dialog, IDC_SUB_TRACK_DOWN)) {
        EnableWindow(
            h,
            valid &&
                state.selectedIndex + 1 < static_cast<int>(state.tracks.size())
                ? TRUE
                : FALSE);
    }

    if (HWND h = GetDlgItem(dialog, IDC_SUB_TRACK_TOGGLE)) {
        EnableWindow(h, valid ? TRUE : FALSE);
        SetCustomButtonActive(h, valid && track->included);
    }
    if (HWND h = GetDlgItem(dialog, IDC_SUB_TRACK_SOFT)) {
        EnableWindow(h, valid ? TRUE : FALSE);
        SetCustomButtonActive(
            h,
            valid && track->mode == CutterSubtitleMode::Soft);
    }
    if (HWND h = GetDlgItem(dialog, IDC_SUB_TRACK_BURN)) {
        EnableWindow(h, external ? TRUE : FALSE);
        SetCustomButtonActive(
            h,
            external && track->mode == CutterSubtitleMode::Burn);
    }
    if (HWND h = GetDlgItem(dialog, IDC_SUB_TRACK_TITLE))
        EnableWindow(h, valid ? TRUE : FALSE);

    if (!valid) {
        SetDlgItemTextW(dialog, IDC_SUB_TRACK_TITLE, L"");
        return;
    }

    SetDlgItemTextW(
        dialog,
        IDC_SUB_TRACK_TITLE,
        track->title.c_str());
}

INT_PTR CALLBACK CutterSubtitlesProc(
    HWND dialog,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam) {

    auto* state = reinterpret_cast<CutterSubtitleTrackDialogState*>(
        GetWindowLongPtrW(dialog, DWLP_USER));

    switch (msg) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<CutterSubtitleTrackDialogState*>(lParam);
        SetWindowLongPtrW(
            dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));

        ApplyDarkTheme(dialog);

        CreateDialogCustomButton(
            dialog, IDC_SUB_TRACK_TOGGLE, L"Include / Exclude",
            92, 184, 88, 16);
        CreateDialogCustomButton(
            dialog, IDC_SUB_TRACK_SOFT, L"Soft",
            104, 210, 48, 16);
        CreateDialogCustomButton(
            dialog, IDC_SUB_TRACK_BURN, L"Burn",
            158, 210, 48, 16);

        for (int id : {
                 IDC_SUB_TRACK_LIST,
                 IDC_SUB_TRACK_ADD,
                 IDC_SUB_TRACK_REMOVE,
                 IDC_SUB_TRACK_UP,
                 IDC_SUB_TRACK_DOWN,
                 IDC_SUB_TRACK_TITLE,
                 IDOK,
                 IDCANCEL }) {
            if (HWND child = GetDlgItem(dialog, id)) {
                ApplyDarkTheme(child);
            }
        }
        if (HWND list = GetDlgItem(dialog, IDC_SUB_TRACK_LIST)) {
            SetWindowTheme(list, L"DarkMode_Explorer", nullptr);
        }

        if (state) {
            RefreshCutterSubtitleTrackList(dialog, *state, state->selectedIndex);
            UpdateCutterSubtitleDialogControls(dialog, *state);
        }

        RECT owner{}, dlg{};
        GetWindowRect(g_main, &owner);
        GetWindowRect(dialog, &dlg);
        const int x =
            owner.left +
            ((owner.right - owner.left) - (dlg.right - dlg.left)) / 2;
        const int y =
            owner.top +
            ((owner.bottom - owner.top) - (dlg.bottom - dlg.top)) / 2;
        SetWindowPos(
            dialog, nullptr, x, y, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    }

    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(g_panelBrush);

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetBkColor(dc, C_PANEL);
        SetTextColor(dc, C_TEXT);
        return reinterpret_cast<INT_PTR>(g_panelBrush);
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, RGB(28, 28, 28));
        return reinterpret_cast<INT_PTR>(
            g_editBrush ? g_editBrush : g_panelBrush);
    }

    case WM_COMMAND: {
        if (!state) break;

        const int id = LOWORD(wParam);
        HWND list = GetDlgItem(dialog, IDC_SUB_TRACK_LIST);
        const int selected = list
            ? static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0))
            : LB_ERR;

        if (id == IDC_SUB_TRACK_LIST &&
            HIWORD(wParam) == LBN_SELCHANGE) {
            CommitCutterSubtitleDialogTitle(dialog, *state);
            state->selectedIndex = selected;
            UpdateCutterSubtitleDialogControls(dialog, *state);
            return TRUE;
        }

        if (id == IDC_SUB_TRACK_ADD) {
            CommitCutterSubtitleDialogTitle(dialog, *state);
            const auto paths = ChooseSubtitleFiles(dialog);
            if (!paths.empty()) {
                int firstNew = static_cast<int>(state->tracks.size());
                for (const auto& rawPath : paths) {
                    const std::wstring path = AbsoluteLocalPath(rawPath);
                    if (path.empty()) continue;

                    const bool duplicate = std::any_of(
                        state->tracks.begin(),
                        state->tracks.end(),
                        [&](const CutterManagedSubtitleTrack& track) {
                            return !track.embedded &&
                                _wcsicmp(
                                    AbsoluteLocalPath(
                                        track.externalPath).c_str(),
                                    path.c_str()) == 0;
                        });
                    if (duplicate) continue;

                    CutterManagedSubtitleTrack track{};
                    track.embedded = false;
                    track.externalPath = path;
                    track.included = true;
                    track.mode = CutterSubtitleMode::Soft;
                    state->tracks.push_back(std::move(track));
                }

                if (firstNew < static_cast<int>(state->tracks.size())) {
                    state->selectedIndex = firstNew;
                }
                RefreshCutterSubtitleTrackList(
                    dialog, *state, state->selectedIndex);
                UpdateCutterSubtitleDialogControls(dialog, *state);
            }
            return TRUE;
        }

        if (id == IDC_SUB_TRACK_REMOVE) {
            CommitCutterSubtitleDialogTitle(dialog, *state);
            if (selected >= 0 &&
                selected < static_cast<int>(state->tracks.size()) &&
                !state->tracks[static_cast<size_t>(selected)].embedded) {
                state->tracks.erase(state->tracks.begin() + selected);
                state->selectedIndex = std::min(
                    selected,
                    static_cast<int>(state->tracks.size()) - 1);
                RefreshCutterSubtitleTrackList(
                    dialog, *state, state->selectedIndex);
                UpdateCutterSubtitleDialogControls(dialog, *state);
            }
            return TRUE;
        }

        if (id == IDC_SUB_TRACK_UP || id == IDC_SUB_TRACK_DOWN) {
            CommitCutterSubtitleDialogTitle(dialog, *state);
            const int delta = id == IDC_SUB_TRACK_UP ? -1 : 1;
            const int other = selected + delta;
            if (selected >= 0 &&
                selected < static_cast<int>(state->tracks.size()) &&
                other >= 0 &&
                other < static_cast<int>(state->tracks.size())) {
                std::swap(
                    state->tracks[static_cast<size_t>(selected)],
                    state->tracks[static_cast<size_t>(other)]);
                state->selectedIndex = other;
                RefreshCutterSubtitleTrackList(
                    dialog, *state, other);
                UpdateCutterSubtitleDialogControls(dialog, *state);
            }
            return TRUE;
        }

        if (id == IDC_SUB_TRACK_TOGGLE ||
            id == IDC_SUB_TRACK_SOFT ||
            id == IDC_SUB_TRACK_BURN) {

            CommitCutterSubtitleDialogTitle(dialog, *state);
            if (selected < 0 ||
                selected >= static_cast<int>(state->tracks.size())) {
                return TRUE;
            }

            auto& track =
                state->tracks[static_cast<size_t>(selected)];

            if (id == IDC_SUB_TRACK_TOGGLE) {
                // Inclusion and mode are intentionally independent. Excluding a
                // row does not erase whether it was Soft or Burn, so re-including
                // it restores the same mode.
                track.included = !track.included;
            } else if (id == IDC_SUB_TRACK_SOFT) {
                track.mode = CutterSubtitleMode::Soft;
            } else {
                if (track.embedded) {
                    UpdateCutterSubtitleDialogControls(dialog, *state);
                    return TRUE;
                }

                // Burn is exclusive across the managed subtitle list, even for
                // an excluded row whose mode is being remembered for later.
                for (auto& otherTrack : state->tracks) {
                    if (&otherTrack == &track) continue;
                    if (otherTrack.mode == CutterSubtitleMode::Burn) {
                        otherTrack.mode = CutterSubtitleMode::Soft;
                    }
                }
                track.mode = CutterSubtitleMode::Burn;
            }

            RefreshCutterSubtitleTrackList(
                dialog, *state, selected);
            UpdateCutterSubtitleDialogControls(dialog, *state);
            return TRUE;
        }

        if (id == IDOK) {
            CommitCutterSubtitleDialogTitle(dialog, *state);
            EndDialog(dialog, IDOK);
            return TRUE;
        }

        if (id == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }

    return FALSE;
}

void ShowCutterSubtitleTrackManager() {
    if (g_cutInputs.size() > 1) {
        MessageBoxW(
            g_main,
            L"Subtitle track management is currently limited to single-file jobs.\n\n"
            L"MULTI joins need a separate ffprobe-based track-matching pass.",
            kAppTitle,
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    StoreLegacySubtitleSyncToManaged();

    const bool wasInitialized = g_cutSubtitleTracksInitialized;
    const auto previousTracks = g_cutSubtitleTracks;
    const int previousSyncIndex = g_cutSubtitleSyncTrackIndex;

    std::wstring activeExternalPath;
    bool activeWasEmbedded = false;
    int activeEmbeddedFfmpegIndex = -1;
    int64_t activeEmbeddedMpvId = -1;
    if (const auto* active = ActiveManagedSubtitleSyncTrackConst()) {
        activeWasEmbedded = active->embedded;
        if (active->embedded) {
            activeEmbeddedFfmpegIndex = active->ffmpegStreamIndex;
            activeEmbeddedMpvId = active->mpvTrackId;
        } else {
            activeExternalPath = AbsoluteLocalPath(active->externalPath);
        }
    } else if (!g_cutSubtitlePath.empty()) {
        activeExternalPath = AbsoluteLocalPath(g_cutSubtitlePath);
    }

    EnsureCutterSubtitleTracksInitialized();

    CutterSubtitleTrackDialogState state;
    state.tracks = g_cutSubtitleTracks;
    state.selectedIndex =
        g_cutSubtitleSyncTrackIndex >= 0
            ? g_cutSubtitleSyncTrackIndex
            : (state.tracks.empty() ? -1 : 0);

    const INT_PTR result = DialogBoxParamW(
        g_instance,
        MAKEINTRESOURCEW(206),
        g_main,
        CutterSubtitlesProc,
        reinterpret_cast<LPARAM>(&state));

    if (result != IDOK) {
        if (!wasInitialized) {
            g_cutSubtitleTracks = previousTracks;
            g_cutSubtitleTracksInitialized = false;
            g_cutSubtitleSyncTrackIndex = previousSyncIndex;
        }
            SyncCutterSubtitleContextControls();
        return;
    }

    g_cutSubtitleTracks = std::move(state.tracks);
    g_cutSubtitleTracksInitialized = true;

    g_cutSubtitleSyncTrackIndex = -1;
    if (activeWasEmbedded) {
        for (size_t i = 0; i < g_cutSubtitleTracks.size(); ++i) {
            const auto& track = g_cutSubtitleTracks[i];
            if (!track.embedded) continue;
            const bool sameFfmpeg =
                activeEmbeddedFfmpegIndex >= 0 &&
                track.ffmpegStreamIndex == activeEmbeddedFfmpegIndex;
            const bool sameMpv =
                activeEmbeddedMpvId >= 0 &&
                track.mpvTrackId == activeEmbeddedMpvId;
            if ((sameFfmpeg || sameMpv) &&
                CutterManagedSubtitleSyncCapable(track)) {
                g_cutSubtitleSyncTrackIndex = static_cast<int>(i);
                break;
            }
        }
    } else if (!activeExternalPath.empty()) {
        for (size_t i = 0; i < g_cutSubtitleTracks.size(); ++i) {
            const auto& track = g_cutSubtitleTracks[i];
            if (track.embedded) continue;
            if (_wcsicmp(
                    AbsoluteLocalPath(track.externalPath).c_str(),
                    activeExternalPath.c_str()) == 0) {
                g_cutSubtitleSyncTrackIndex = static_cast<int>(i);
                break;
            }
        }
    }
    if (g_cutSubtitleSyncTrackIndex < 0) {
        for (size_t i = 0; i < g_cutSubtitleTracks.size(); ++i) {
            if (CutterManagedSubtitleSyncCapable(g_cutSubtitleTracks[i])) {
                g_cutSubtitleSyncTrackIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (g_cutSubtitleSyncTrackIndex >= 0) {
        LoadManagedSubtitleSyncToLegacy();
    } else {
        g_cutSubtitlePath.clear();
        g_cutSubtitleTitle.clear();
        g_cutSubtitlesEnabled = false;
        g_cutSubtitleOffsetMs = 0;
        g_cutSubtitleSyncASet = false;
        g_cutSubtitleSyncBSet = false;
        g_cutSubtitleDriftEnabled = false;
    }

    SaveSettings();
    SyncCutterSubtitleContextControls();
    if (g_cutSubtitles) InvalidateRect(g_cutSubtitles, nullptr, FALSE);
    if (g_cutSubSyncButton) InvalidateRect(g_cutSubSyncButton, nullptr, FALSE);

    if (CutterHasActiveSubtitleSyncTarget()) {
        CutterSelectActiveSubtitleForPreview();
        CutterApplySubtitlePreviewDelay();
    } else if (g_mpv) {
        MpvSetDouble("sub-delay", 0.0);
        g_subtitleDelayTouched = false;
    }

    const size_t included = static_cast<size_t>(std::count_if(
        g_cutSubtitleTracks.begin(),
        g_cutSubtitleTracks.end(),
        [](const CutterManagedSubtitleTrack& track) {
            return track.included;
        }));
    ShowOsdText(
        L"Output subtitle tracks: " + std::to_wstring(included),
        1500);
}

INT_PTR CALLBACK CutterOptionsProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        ApplyDarkTheme(dialog);
        const int ids[] = { IDC_OPT_OVERWRITE, IDC_OPT_SHOW_POWERSHELL, IDC_OPT_CLOSE_POWERSHELL, IDOK, IDCANCEL };
        for (int id : ids) {
            HWND child = GetDlgItem(dialog, id);
            if (child) ApplyDarkTheme(child);
        }

        CheckDlgButton(dialog, IDC_OPT_OVERWRITE,
                       g_cutOverwriteEnabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_OPT_SHOW_POWERSHELL,
                       g_cutShowPowerShell ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_OPT_CLOSE_POWERSHELL,
                       g_cutClosePowerShellWhenDone ? BST_CHECKED : BST_UNCHECKED);

        RECT owner{};
        RECT dlg{};
        GetWindowRect(g_main, &owner);
        GetWindowRect(dialog, &dlg);
        const int x = owner.left +
            ((owner.right - owner.left) - (dlg.right - dlg.left)) / 2;
        const int y = owner.top +
            ((owner.bottom - owner.top) - (dlg.bottom - dlg.top)) / 2;
        SetWindowPos(dialog, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    }
    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(g_panelBrush);

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_PANEL);
        return reinterpret_cast<INT_PTR>(g_panelBrush);
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, RGB(28, 28, 28));
        return reinterpret_cast<INT_PTR>(g_editBrush ? g_editBrush : g_panelBrush);
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            g_cutOverwriteEnabled =
                IsDlgButtonChecked(dialog, IDC_OPT_OVERWRITE) == BST_CHECKED;
            g_cutShowPowerShell =
                IsDlgButtonChecked(dialog, IDC_OPT_SHOW_POWERSHELL) == BST_CHECKED;
            g_cutClosePowerShellWhenDone =
                IsDlgButtonChecked(dialog, IDC_OPT_CLOSE_POWERSHELL) == BST_CHECKED;
            ShowOsdText(L"Cutter options saved", 1400);
            if (g_cutOptions) InvalidateRect(g_cutOptions, nullptr, FALSE);
            SaveSettings();
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK AboutProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        ApplyDarkTheme(dialog);

        HWND ok = GetDlgItem(dialog, IDOK);
        if (ok) ApplyDarkTheme(ok);
        HWND github = GetDlgItem(dialog, IDC_ABOUT_GITHUB);
        if (github) ApplyDarkTheme(github);

        RECT owner{}, dlg{};
        GetWindowRect(g_main, &owner);
        GetWindowRect(dialog, &dlg);
        const int x = owner.left +
            ((owner.right - owner.left) - (dlg.right - dlg.left)) / 2;
        const int y = owner.top +
            ((owner.bottom - owner.top) - (dlg.bottom - dlg.top)) / 2;
        SetWindowPos(dialog, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    }

    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(g_panelBrush);

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, C_TEXT);
        return reinterpret_cast<INT_PTR>(g_panelBrush);
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_ABOUT_GITHUB) {
            ShellExecuteW(dialog, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void HandleCommand(int id) {
    switch (id) {
    case IDC_CUT_OPEN:
        OpenFileDialog();
        break;
    case IDC_CUT_MULTI:
        ShowCutterInputManager();
        break;
    case IDC_CUT_TRACKS:
        ShowCutterAudioTrackManager();
        break;
    case IDC_CUT_AUDIO_SYNC:
        if (g_cutInputs.size() > 1) {
            MessageBoxW(
                g_main,
                L"Audio sync is currently limited to single-file jobs.\n\n"
                L"MULTI joins need per-file audio track matching first.",
                kAppTitle,
                MB_OK | MB_ICONINFORMATION);
            break;
        }
        if (g_cutInputs.empty() && g_currentMediaPath.empty()) {
            MessageBoxW(
                g_main,
                L"Open an input file first.",
                kAppTitle,
                MB_OK | MB_ICONINFORMATION);
            break;
        }
        {
            const bool open = !g_cutAudioSyncOpen;
            if (open) {
                EnsureCutterAudioTracksInitialized();
                if (g_mpvReady) {
                    g_cutAudioPreviewBaseDelaySeconds =
                        MpvGetDouble("audio-delay", 0.0);
                    g_cutAudioPreviewSession = true;
                }
                RefreshCutterAudioSyncTrackCombo();
                LoadManagedAudioSyncToControls();
            }
            SetCutterAudioSyncOpen(open);
        }
        break;
    case IDC_AUDIO_SYNC_TRACK: {
        if (!g_cutAudioSyncTrack || !g_cutAudioTracksInitialized) break;
        const int comboIndex = static_cast<int>(
            SendMessageW(g_cutAudioSyncTrack, CB_GETCURSEL, 0, 0));
        if (comboIndex == CB_ERR) break;
        const LRESULT itemData = SendMessageW(
            g_cutAudioSyncTrack, CB_GETITEMDATA, comboIndex, 0);
        if (itemData == CB_ERR) break;
        const int trackIndex = static_cast<int>(itemData);
        if (CutterSelectManagedAudioSyncTrack(trackIndex)) {
            if (const auto* track = ActiveManagedAudioSyncTrackConst()) {
                ShowOsdText(
                    L"Audio sync target: " + ManagedAudioSyncLabel(*track),
                    1300);
            }
        }
        break;
    }
    case IDC_CUT_MARK_A:
        SetCutterMark(true);
        break;
    case IDC_CUT_MARK_B:
        SetCutterMark(false);
        break;
    case IDC_CUT_FRAME_PREV:
        FrameStep(false);
        break;
    case IDC_CUT_FRAME_NEXT:
        FrameStep(true);
        break;
    case IDC_CUT_CROP:
        BeginCutterCropSelection();
        break;
    case IDC_CUT_SUBTITLES:
        ShowCutterSubtitleTrackManager();
        break;
    case IDC_CUT_SUB_SYNC: {
        const bool open = !g_cutSubtitleSyncOpen;
        if (open && g_cutInputs.size() <= 1) {
            StoreLegacySubtitleSyncToManaged();
            EnsureCutterSubtitleTracksInitialized();
            if (g_cutSubtitleSyncTrackIndex >= 0) {
                LoadManagedSubtitleSyncToLegacy();
            }
        }
        SetCutterSubtitleSyncOpen(open);
        if (open && CutterHasActiveSubtitleSyncTarget()) {
            CutterSelectActiveSubtitleForPreview();
            CutterApplySubtitlePreviewDelay();
        }
        break;
    }
    case IDC_SUB_SYNC_TRACK: {
        if (!g_cutSubSyncTrack || !g_cutSubtitleTracksInitialized) break;
        const int comboIndex = static_cast<int>(
            SendMessageW(g_cutSubSyncTrack, CB_GETCURSEL, 0, 0));
        if (comboIndex == CB_ERR) break;
        const LRESULT itemData = SendMessageW(
            g_cutSubSyncTrack, CB_GETITEMDATA, comboIndex, 0);
        if (itemData == CB_ERR) break;
        const int trackIndex = static_cast<int>(itemData);
        if (CutterSelectManagedSubtitleSyncTrack(trackIndex)) {
            const auto* track = ActiveManagedSubtitleSyncTrackConst();
            if (track) {
                ShowOsdText(
                    L"Subtitle sync target: " +
                        ManagedSubtitleSyncLabel(*track),
                    1400);
            }
        }
        break;
    }
    case IDC_SUB_ENABLE:
        g_cutSubtitlesEnabled = !g_cutSubtitlesEnabled;
        SyncCutterSubtitleContextControls();
        SaveSettings();
        ShowOsdText(g_cutSubtitlesEnabled
                        ? L"External subtitles: ON"
                        : L"External subtitles: OFF",
                    1200);
        break;
    case IDC_SUB_BROWSE: {
        const std::wstring path = ChooseSubtitleFile(g_main);
        if (!path.empty()) {
            g_cutSubtitlePath = path;
            g_cutSubtitlesEnabled = true;
            g_cutSubtitleSyncASet = false;
            g_cutSubtitleSyncBSet = false;
            g_cutSubtitleDriftEnabled = false;
            SyncCutterSubtitleContextControls();
            SaveSettings();
            CutterSelectSubtitleForPreview(path);
            ShowOsdText(L"Subtitle file selected: " +
                        std::filesystem::path(path).filename().wstring(),
                        1600);
        }
        break;
    }
    case IDC_SUB_CLEAR:
        g_cutSubtitlePath.clear();
        g_cutSubtitleTitle.clear();
        g_cutSubtitlesEnabled = false;
        g_cutSubtitleSyncASet = false;
        g_cutSubtitleSyncBSet = false;
        g_cutSubtitleDriftEnabled = false;
        SyncCutterSubtitleContextControls();
        SaveSettings();
        ShowOsdText(L"External subtitles cleared", 1200);
        break;
    case IDC_SUB_SOFT:
        g_cutSubtitleMode = CutterSubtitleMode::Soft;
        SyncCutterSubtitleContextControls();
        SaveSettings();
        ShowOsdText(L"Subtitle mode: Soft", 1100);
        break;
    case IDC_SUB_BURN:
        g_cutSubtitleMode = CutterSubtitleMode::Burn;
        SyncCutterSubtitleContextControls();
        SaveSettings();
        ShowOsdText(L"Subtitle mode: Burn", 1100);
        break;
    case IDC_SUB_KEEP_EMBEDDED:
        g_cutKeepEmbeddedSubtitles = !g_cutKeepEmbeddedSubtitles;
        SyncCutterSubtitleContextControls();
        SaveSettings();
        ShowOsdText(g_cutKeepEmbeddedSubtitles
                        ? L"Keep embedded subtitles: ON"
                        : L"Keep embedded subtitles: OFF",
                    1300);
        break;
    case IDC_SUB_OFFSET_MINUS:
        CutterAdjustSubtitleOffsetMs(-50);
        ShowOsdText(
            L"Subtitle offset: " +
            std::to_wstring(g_cutSubtitleOffsetMs) +
            L" ms",
            1000);
        break;
    case IDC_SUB_OFFSET_PLUS:
        CutterAdjustSubtitleOffsetMs(50);
        ShowOsdText(
            L"Subtitle offset: " +
            std::to_wstring(g_cutSubtitleOffsetMs) +
            L" ms",
            1000);
        break;
    case IDC_SUB_OFFSET_RESET:
        CutterSetSubtitleOffsetMs(0, true);
        ShowOsdText(L"Subtitle offset: 0 ms", 1000);
        break;
    case IDC_SUB_OFFSET: {
        int parsed = g_cutSubtitleOffsetMs;
        if (CutterParseSubtitleOffsetText(parsed, true)) {
            CutterSetSubtitleOffsetMs(parsed, true);
        }
        break;
    }
    case IDC_SUB_SYNC_A:
        CutterCaptureSubtitleSyncPoint(true);
        break;
    case IDC_SUB_SYNC_B:
        CutterCaptureSubtitleSyncPoint(false);
        break;
    case IDC_SUB_DRIFT:
        if (!CutterSubtitleDriftReady()) {
            ShowOsdText(
                L"Set SYNC A and SYNC B first",
                1500);
        } else {
            g_cutSubtitleDriftEnabled =
                !g_cutSubtitleDriftEnabled;
            SyncCutterSubtitleContextControls();
            CutterApplySubtitlePreviewDelay();
            ShowOsdText(
                g_cutSubtitleDriftEnabled
                    ? L"Subtitle drift correction: ON"
                    : L"Subtitle drift correction: OFF",
                1400);
        }
        break;
    case IDC_SUB_SYNC_CLEAR:
        CutterClearSubtitleDrift();
        break;
    case IDC_SUB_PATH: {
        wchar_t path[32768]{};
        GetWindowTextW(g_cutSubPathEdit, path,
                       static_cast<int>(std::size(path)));
        g_cutSubtitlePath = path;
        g_cutSubtitleSyncASet = false;
        g_cutSubtitleSyncBSet = false;
        g_cutSubtitleDriftEnabled = false;
        SyncCutterSubtitleContextControls();
        SaveSettings();
        break;
    }
    case IDC_SUB_TITLE: {
        wchar_t title[512]{};
        GetWindowTextW(g_cutSubTitleEdit, title,
                       static_cast<int>(std::size(title)));
        g_cutSubtitleTitle = title;
        SaveSettings();
        break;
    }
    case IDC_CUT_STICK_KEYFRAMES:
        g_cutStickKeyframes = !g_cutStickKeyframes;
        if (g_cutStickKeyframesButton) InvalidateRect(g_cutStickKeyframesButton, nullptr, FALSE);
        SaveSettings();
        ShowOsdText(g_cutStickKeyframes ? L"Stick to keyframes: ON" : L"Stick to keyframes: OFF", 1300);
        break;
    case IDC_CUT_OPTIONS:
        DialogBoxParamW(g_instance, MAKEINTRESOURCEW(204), g_main,
                        CutterOptionsProc, 0);
        break;
    case IDC_CUT_FORMAT: {
        if (g_cutFormat) {
            const int selection = static_cast<int>(SendMessageW(g_cutFormat, CB_GETCURSEL, 0, 0));
            if (selection >= 0 && selection <= 6) {
                g_cutMode = static_cast<CutterOutputMode>(selection);
                g_cutOutputPath.clear();
                if (!CutterReencodesVideo() &&
                    (g_cutCropActive || g_cutCropSelecting)) {
                    DisableCutterCrop();
                }
                SyncCutterControls();
                SaveSettings();
            }
        }
        break;
    }
    case IDC_CUT_RESOLUTION: {
        if (g_cutResolution) {
            const int selection = static_cast<int>(SendMessageW(g_cutResolution, CB_GETCURSEL, 0, 0));
            const wchar_t* values[] = { L"Source", L"1080p", L"720p", L"480p", L"360p" };
            if (selection >= 0 && selection < static_cast<int>(std::size(values))) {
                g_cutResolutionValue = values[selection];
                g_cutOutputPath.clear();
                SaveSettings();
            }
        }
        break;
    }
    case IDC_CUT_SIZE:
        CutterReadSizeField();
        break;
    case IDC_CUT_CRF:
        CutterReadCrfField();
        break;
    case IDC_CUT_TWO_PASS:
        g_cutTwoPass = !g_cutTwoPass;
        if (g_cutTwoPassButton) InvalidateRect(g_cutTwoPassButton, nullptr, FALSE);
        SaveSettings();
        ShowOsdText(g_cutTwoPass ? L"2-pass target encoding: ON" :
                                   L"2-pass target encoding: OFF", 1300);
        break;
    case IDC_CUT_AUDIO_MODE: {
        if (g_cutAudioModeCombo) {
            const int selection = static_cast<int>(SendMessageW(g_cutAudioModeCombo, CB_GETCURSEL, 0, 0));
            if (selection >= 0 && selection <= 2) {
                g_cutAudioMode = static_cast<CutterAudioMode>(selection);
                if (g_cutAudioMode == CutterAudioMode::Auto) {
                    g_cutAudioKbps = 128;
                } else {
                    g_cutAudioKbps = 0;
                }
                SyncCutterAudioControls(true);
                SaveSettings();
            }
        }
        break;
    }
    case IDC_CUT_AUDIO_KBPS: {
        int audioK = 0;
        if (CutterParseAudioKbpsField(audioK, false) && g_cutAudioMode == CutterAudioMode::Auto) {
            g_cutAudioKbps = audioK;
            SaveSettings();
        }
        break;
    }
    case IDC_CUT_AUDIO_OFFSET_MINUS:
        AdjustCutterAudioOffset(-50);
        break;
    case IDC_CUT_AUDIO_OFFSET_PLUS:
        AdjustCutterAudioOffset(50);
        break;
    case IDC_AUDIO_SYNC_OFFSET_RESET:
        if (auto* track = ActiveManagedAudioSyncTrack()) {
            track->offsetMs = 0;
            LoadManagedAudioSyncToControls();
            CutterApplyAudioPreviewDelay();
            if (g_cutAudioOffsetReset) InvalidateRect(g_cutAudioOffsetReset, nullptr, FALSE);
            ShowOsdText(L"Audio offset: 0 ms", 1000);
        }
        break;
    case IDC_CUT_AUDIO_OFFSET:
        CutterReadAudioOffsetField();
        break;
    case IDC_CUT_OUTPUT:
        ChooseCutterOutput();
        break;
    case IDC_CUT_CLEAR:
        ClearCutterInputs();
        break;
    case IDC_CUT_NUKE:
        NukeCutter();
        break;
    case IDC_CUT_CONVERT:
        StartCutterConvert();
        break;
    case IDC_PLAY:
    case IDM_PLAY_TOGGLE:
        TogglePlay(); break;
    case IDC_STOP:
    case IDM_PLAY_STOP:
        StopPlayback(); break;
    case IDC_PREV:
    case IDM_PLAY_PREV:
        ChapterStep(false); break;
    case IDC_SEEK_BACK:
    case IDM_PLAY_BACK:
        SeekBy(-kSeekStepSeconds); break;
    case IDC_SEEK_FORWARD:
    case IDM_PLAY_FORWARD:
        SeekBy(kSeekStepSeconds); break;
    case IDC_NEXT:
    case IDM_PLAY_NEXT:
        ChapterStep(true); break;
    case IDC_SPEED_DOWN:
    case IDM_SPEED_DOWN:
        AdjustSpeed(-0.1); break;
    case IDC_SPEED_UP:
    case IDM_SPEED_UP:
        AdjustSpeed(0.1); break;
    case IDC_AUTO_NEXT:
    case IDM_AUTO_NEXT_FILE:
        ToggleAutoPlayNextFile(); break;
    case IDC_LOOP_CURRENT:
    case IDM_LOOP_CURRENT_FILE:
        ToggleLoopCurrentFile(); break;
    case IDM_FILE_SCREENSHOT:
        TakeScreenshot(); break;
    case IDC_PLAYLIST_OSD:
        TogglePlaylistOsd(); break;
    case IDM_PLAYLIST_SHOW:
        TogglePlaylistOsd(); break;
    case IDC_PLAYLIST_MENU:
        ShowPlaylistPopup(g_playlistMenu); break;
    case IDC_PLAYLIST_PREV_BUTTON:
    case IDM_PLAYLIST_PREV:
        PlaylistStep(false); break;
    case IDC_PLAYLIST_NEXT_BUTTON:
    case IDM_PLAYLIST_NEXT:
        PlaylistStep(true); break;
    case IDC_AUDIO:
    case IDM_AUDIO_TRACKS:
        ShowTrackMenu(g_audio, true); break;
    case IDC_SUBS:
    case IDM_SUB_TRACKS:
        ShowTrackMenu(g_subs, false); break;
    case IDC_MUTE:
    case IDM_MUTE:
        ToggleMute(); break;
    case IDM_FILE_OPEN:
        OpenFileDialog(); break;
    case IDM_VIEW_FULLSCREEN:
        ToggleFullscreen(); break;
    case IDM_VIEW_MEDIA_INFO:
        ToggleMediaInfo(); break;
    case IDM_VIEW_OPTIONS:
        if (DialogBoxParamW(g_instance, MAKEINTRESOURCEW(201), g_main,
                            OptionsProc, 0) == IDOK) {
            // OptionsProc applies settings while the modal dialog is still active.
            // Run one final layout after it has actually closed so controls that
            // appear/disappear (notably optional Stop and PREV/NEXT buttons)
            // cannot leave PLAYLIST / SUB / AUD using an intermediate geometry.
            LayoutChildren(g_main);
            if (g_panel) {
                RedrawWindow(g_panel, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            }
        }
        break;
    case IDM_VIEW_CUTTER_OPTIONS:
        DialogBoxParamW(g_instance, MAKEINTRESOURCEW(204), g_main,
                        CutterOptionsProc, 0);
        break;
    case IDM_VIEW_OPEN_MPV_CONFIG:
        OpenMpvConfig();
        break;
    case IDM_FILE_EXIT:
        DestroyWindow(g_main); break;
    case IDM_HELP_ABOUT:
        DialogBoxParamW(g_instance, MAKEINTRESOURCEW(202), g_main,
                        AboutProc, 0);
        break;
    }
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_taskbarButtonCreatedMsg != 0 &&
        msg == g_taskbarButtonCreatedMsg) {
        InitializeTaskbarButton();
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        ApplyDarkTheme(hwnd);
        CreateUi(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        return 0;
    case WM_SIZE:
        if (!g_suppressFullscreenSizeLayout) {
            LayoutChildren(hwnd);
            if (g_playlistOsdVisible) RenderPlaylistOverlay();
        }
        return 0;
    case WM_DISPLAYCHANGE:
        if (g_fullscreen) RefreshFullscreenMonitorBounds(hwnd);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 900;
        info->ptMinTrackSize.y = 400;
        return 0;
    }
    case WM_APPCOMMAND:
        if (HandleMediaAppCommand(lParam)) return TRUE;
        break;
    case WM_COPYDATA: {
        const auto* copy = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (!copy) return FALSE;

        if (copy->dwData == kCopyDataPausePlayback) {
            PausePlaybackIfPlaying();
            return static_cast<LRESULT>(kCopyDataAck);
        }

        if (copy->dwData == kCopyDataOpenFiles) {
            if (!g_mpvReady || !copy->lpData ||
                copy->cbData < sizeof(wchar_t) * 2 ||
                copy->cbData % sizeof(wchar_t) != 0) {
                return FALSE;
            }

            const auto* data = static_cast<const wchar_t*>(copy->lpData);
            const size_t count = copy->cbData / sizeof(wchar_t);
            std::vector<std::wstring> paths;

            size_t offset = 0;
            while (offset < count && data[offset] != L'\0') {
                size_t end = offset;
                while (end < count && data[end] != L'\0') ++end;
                if (end >= count) return FALSE;
                paths.emplace_back(data + offset, end - offset);
                offset = end + 1;
            }

            if (paths.empty()) return FALSE;
            LoadPlaylist(paths);

            if (IsIconic(g_main)) ShowWindow(g_main, SW_RESTORE);
            SetForegroundWindow(g_main);
            return static_cast<LRESULT>(kCopyDataAck);
        }
        return FALSE;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == THBN_CLICKED) {
            switch (LOWORD(wParam)) {
            case kTaskbarPreviousButtonId:
                PlaylistStep(false); return 0;
            case kTaskbarStopButtonId:
                StopPlayback(); return 0;
            case kTaskbarPlayPauseButtonId:
                TogglePlay(); return 0;
            case kTaskbarNextButtonId:
                PlaylistStep(true); return 0;
            case kTaskbarFullscreenButtonId:
                ToggleFullscreenFromTaskbar(); return 0;
            }
        }
        {
            const int commandId = LOWORD(wParam);
            const int notifyCode = HIWORD(wParam);

            // Subtitle text is live editor state. Keep the globals current on
            // EN_CHANGE so clicking one of our owner-drawn buttons does not
            // depend on that custom control taking keyboard focus. Persist only
            // on focus loss or another subtitle action.
            if (commandId == IDC_SUB_PATH && notifyCode == EN_CHANGE) {
                wchar_t path[32768]{};
                GetWindowTextW(g_cutSubPathEdit, path,
                               static_cast<int>(std::size(path)));
                g_cutSubtitlePath = path;
                return 0;
            }
            if (commandId == IDC_SUB_TITLE && notifyCode == EN_CHANGE) {
                wchar_t title[512]{};
                GetWindowTextW(g_cutSubTitleEdit, title,
                               static_cast<int>(std::size(title)));
                g_cutSubtitleTitle = title;
                return 0;
            }

            // Cutter edits update/persist only when the user leaves the field.
            // Programmatic SetWindowText and every typed character generate EN_CHANGE;
            // treating those as full commands caused unnecessary disk writes.
            if (commandId == IDC_CUT_SIZE ||
                commandId == IDC_CUT_CRF ||
                commandId == IDC_CUT_AUDIO_KBPS ||
                commandId == IDC_CUT_AUDIO_OFFSET ||
                commandId == IDC_SUB_PATH ||
                commandId == IDC_SUB_TITLE ||
                commandId == IDC_SUB_OFFSET) {
                if (notifyCode != EN_KILLFOCUS) return 0;
            }

            // Combos only act on an actual selection change, not open/close/focus noise.
            if (commandId == IDC_CUT_FORMAT ||
                commandId == IDC_CUT_RESOLUTION ||
                commandId == IDC_CUT_AUDIO_MODE ||
                commandId == IDC_SUB_SYNC_TRACK) {
                if (notifyCode != CBN_SELCHANGE) return 0;
            }

            HandleCommand(commandId);
        }
        return 0;
    case WM_APP_FINAL_FULLSCREEN_REDRAW:
        g_finalFullscreenRedrawPending = false;
        RedrawWindow(hwnd, nullptr, nullptr, RDW_UPDATENOW | RDW_NOCHILDREN);
        return 0;
    case WM_APP_AUTO_NEXT_FILE:
        PlayPendingAutoNextFile();
        return 0;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> mediaPaths;
        std::vector<std::wstring> subtitlePaths;
        mediaPaths.reserve(count);
        subtitlePaths.reserve(count);

        for (UINT i = 0; i < count; ++i) {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0);
            if (length == 0) continue;
            std::vector<wchar_t> path(static_cast<size_t>(length) + 1, L'\0');
            if (DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size()))) {
                const std::wstring value = AbsoluteLocalPath(path.data());
                if (CutterIsSubtitlePath(value)) subtitlePaths.push_back(value);
                else mediaPaths.push_back(value);
            }
        }

        DragFinish(drop);
        if (!mediaPaths.empty()) LoadPlaylist(mediaPaths);
        if (!subtitlePaths.empty()) {
            if (g_cutInputs.size() > 1) {
                // Keep the old single-external-subtitle behavior for MULTI.
                // The managed subtitle list is deliberately single-input.
                g_cutSubtitlePath = subtitlePaths.front();
                g_cutSubtitlesEnabled = true;
                g_cutSubtitleSyncASet = false;
                g_cutSubtitleSyncBSet = false;
                g_cutSubtitleDriftEnabled = false;
                SaveSettings();
                CutterSelectSubtitleForPreview(g_cutSubtitlePath);
                SyncCutterSubtitleContextControls();
                if (g_cutSubtitles) {
                    InvalidateRect(g_cutSubtitles, nullptr, FALSE);
                }
                ShowOsdText(
                    L"MULTI: using first dropped subtitle only - " +
                        std::filesystem::path(
                            g_cutSubtitlePath).filename().wstring(),
                    2000);
            } else {
                StoreLegacySubtitleSyncToManaged();
                EnsureCutterSubtitleTracksInitialized();

                int firstDroppedIndex = -1;
                for (const auto& rawPath : subtitlePaths) {
                    const std::wstring path = AbsoluteLocalPath(rawPath);
                    if (path.empty()) continue;

                    int existingIndex = -1;
                    for (size_t i = 0; i < g_cutSubtitleTracks.size(); ++i) {
                        const auto& track = g_cutSubtitleTracks[i];
                        if (!track.embedded &&
                            _wcsicmp(
                                AbsoluteLocalPath(
                                    track.externalPath).c_str(),
                                path.c_str()) == 0) {
                            existingIndex = static_cast<int>(i);
                            break;
                        }
                    }

                    if (existingIndex >= 0) {
                        if (firstDroppedIndex < 0) {
                            firstDroppedIndex = existingIndex;
                        }
                        continue;
                    }

                    CutterManagedSubtitleTrack track{};
                    track.embedded = false;
                    track.externalPath = path;
                    track.included = true;
                    track.mode = CutterSubtitleMode::Soft;
                    const int newIndex =
                        static_cast<int>(g_cutSubtitleTracks.size());
                    g_cutSubtitleTracks.push_back(std::move(track));
                    if (firstDroppedIndex < 0) {
                        firstDroppedIndex = newIndex;
                    }
                }

                if (firstDroppedIndex >= 0) {
                    g_cutSubtitleSyncTrackIndex = firstDroppedIndex;
                    LoadManagedSubtitleSyncToLegacy();
                }

                SaveSettings();
                            SyncCutterSubtitleContextControls();
                if (g_cutSubtitles) {
                    InvalidateRect(g_cutSubtitles, nullptr, FALSE);
                }

                if (CutterHasActiveSubtitleSyncTarget()) {
                    CutterSelectActiveSubtitleForPreview();
                    CutterApplySubtitlePreviewDelay();
                }

                if (subtitlePaths.size() == 1) {
                    ShowOsdText(
                        L"Subtitle file added: " +
                            std::filesystem::path(
                                subtitlePaths.front()).filename().wstring(),
                        1800);
                } else {
                    ShowOsdText(
                        L"Subtitle files added: " +
                            std::to_wstring(subtitlePaths.size()),
                        1800);
                }
            }
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == TIMER_SINGLE_CLICK) {
            KillTimer(hwnd, TIMER_SINGLE_CLICK);
            if (g_pendingSingleClick) {
                g_pendingSingleClick = false;
                TogglePlay();
                g_singleClickApplied = true;
            }
            return 0;
        }
        if (wParam == TIMER_FULLSCREEN_HOVER) {
            CheckFullscreenControlsHover();
            return 0;
        }
        if (wParam == TIMER_MPV_POLL) {
            PollMpv();
            PollCutterWorker();
            CheckWindowedCursorHide();
            return 0;
        }
        if (wParam == TIMER_PLAYLIST_OSD_RESTORE) {
            KillTimer(hwnd, TIMER_PLAYLIST_OSD_RESTORE);
            if (g_playlistOsdVisible) RenderPlaylistOverlay();
            else RemovePlaylistOverlay();
            return 0;
        }
        if (wParam == TIMER_TASKBAR_ACTIVATE) {
            KillTimer(hwnd, TIMER_TASKBAR_ACTIVATE);
            ActivateMainWindowFromTaskbar();
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        SaveCurrentResumePosition();
        FlushResumePositions();
        SaveSettings();
        KillTimer(hwnd, TIMER_SINGLE_CLICK);
        KillTimer(hwnd, TIMER_FULLSCREEN_HOVER);
        KillTimer(hwnd, TIMER_MPV_POLL);
        KillTimer(hwnd, TIMER_PLAYLIST_OSD_RESTORE);
        KillTimer(hwnd, TIMER_TASKBAR_ACTIVATE);
        SetWindowedCursorHidden(false);
        ReleaseTaskbarInterface();
        DestroyTaskbarGlyphIcons();
        if (g_cutWorkerJob) TerminateJobObject(g_cutWorkerJob, 1);
        CleanupCutterWorker();
        CleanupLingeringCutterConsoles();
        ShutdownMpv();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool RegisterClasses() {
    HICON icon = LoadIconW(g_instance, MAKEINTRESOURCEW(101));
    if (!icon) icon = LoadIconW(nullptr, IDI_APPLICATION);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = icon;
    wc.hIconSm = icon;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kMainClass;
    if (!RegisterClassExW(&wc)) return false;

    wc = WNDCLASSEXW{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = VideoProc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kVideoClass;
    if (!RegisterClassExW(&wc)) return false;

    wc = WNDCLASSEXW{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MenuProc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_panelBrush;
    wc.lpszClassName = kMenuClass;
    if (!RegisterClassExW(&wc)) return false;

    wc = WNDCLASSEXW{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_panelBrush;
    wc.lpszClassName = kPanelClass;
    if (!RegisterClassExW(&wc)) return false;

    wc = WNDCLASSEXW{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = SeekProc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.hbrBackground = g_panelBrush;
    wc.lpszClassName = kSeekClass;
    if (!RegisterClassExW(&wc)) return false;

    wc = WNDCLASSEXW{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ButtonProc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_panelBrush;
    wc.lpszClassName = kButtonClass;
    if (!RegisterClassExW(&wc)) return false;

    wc = WNDCLASSEXW{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kOverlayClass;
    if (!RegisterClassExW(&wc)) return false;

    wc = WNDCLASSEXW{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ProgressProc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_panelBrush;
    wc.lpszClassName = kProgressClass;
    if (!RegisterClassExW(&wc)) return false;

    wc = WNDCLASSEXW{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = VolumeProc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.hbrBackground = g_panelBrush;
    wc.lpszClassName = kVolumeClass;
    return RegisterClassExW(&wc) != 0;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
    LoadSettings();

    int startupArgc = 0;
    LPWSTR* startupArgv = CommandLineToArgvW(GetCommandLineW(), &startupArgc);
    std::vector<std::wstring> startupPaths;
    if (startupArgv && startupArgc > 1) {
        startupPaths.reserve(static_cast<size_t>(startupArgc - 1));
        for (int i = 1; i < startupArgc; ++i) {
            if (startupArgv[i] && *startupArgv[i]) {
                startupPaths.push_back(AbsoluteLocalPath(startupArgv[i]));
            }
        }
    }
    if (startupArgv) LocalFree(startupArgv);

    if (!startupPaths.empty()) {
        HWND existing = FindWindowW(kMainClass, nullptr);
        if (existing) {
            auto sendCopyData = [&](ULONG_PTR kind,
                                    const std::vector<std::wstring>* paths) {
                COPYDATASTRUCT copy{};
                copy.dwData = kind;
                std::vector<wchar_t> payload;

                if (paths) {
                    size_t chars = 1;
                    for (const auto& path : *paths) chars += path.size() + 1;
                    payload.reserve(chars);
                    for (const auto& path : *paths) {
                        payload.insert(payload.end(), path.begin(), path.end());
                        payload.push_back(L'\0');
                    }
                    payload.push_back(L'\0');
                    copy.cbData = static_cast<DWORD>(
                        payload.size() * sizeof(wchar_t));
                    copy.lpData = payload.data();
                }

                DWORD_PTR result = 0;
                const LRESULT sent = SendMessageTimeoutW(
                    existing, WM_COPYDATA, 0,
                    reinterpret_cast<LPARAM>(&copy),
                    SMTO_ABORTIFHUNG | SMTO_BLOCK, 1500, &result);
                // Require our explicit protocol ACK. An older build that does not
                // handle WM_COPYDATA may let DefWindowProc return TRUE (1), which
                // must not be mistaken for a successful file handoff.
                return sent != 0 && result == kCopyDataAck;
            };

            if (g_instanceOpenBehavior ==
                InstanceOpenBehavior::OpenInExisting) {
                if (sendCopyData(kCopyDataOpenFiles, &startupPaths)) {
                    if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
                    SetForegroundWindow(existing);
                    return 0;
                }
                // If an older or busy instance cannot accept the handoff,
                // fall through and open normally rather than losing the file.
            } else if (g_instanceOpenBehavior ==
                       InstanceOpenBehavior::PauseExistingAndOpenNew) {
                sendCopyData(kCopyDataPausePlayback, nullptr);
            }
        }
    }

    LoadResumePositions();
    g_instance = instance;
    const HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(comResult);
    g_taskbarButtonCreatedMsg =
        RegisterWindowMessageW(L"TaskbarButtonCreated");

    EnableDarkModeForApp();
    g_panelBrush = CreateSolidBrush(C_PANEL);
    g_editBrush = CreateSolidBrush(RGB(28, 28, 28));
    g_seekBgBrush = CreateSolidBrush(C_SEEK_BG);
    g_seekFillBrush = CreateSolidBrush(C_SEEK_FILL);
    g_chapterMarkerBrush = CreateSolidBrush(C_CHAPTER_MARKER);
    g_seekPlayheadBrush = CreateSolidBrush(C_TEAL);

    g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_smallFont = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    if (!RegisterClasses()) {
        MessageBoxW(nullptr, L"Could not register the native player window classes.", kAppTitle, MB_OK | MB_ICONERROR);
        if (comInitialized) CoUninitialize();
        return 1;
    }
    BuildMenus();

    const int initialX = g_haveSavedWindowRect
        ? static_cast<int>(g_savedWindowRect.left) : CW_USEDEFAULT;
    const int initialY = g_haveSavedWindowRect
        ? static_cast<int>(g_savedWindowRect.top) : CW_USEDEFAULT;
    const int initialW = g_haveSavedWindowRect
        ? static_cast<int>(g_savedWindowRect.right - g_savedWindowRect.left) : 1060;
    const int initialH = g_haveSavedWindowRect
        ? static_cast<int>(g_savedWindowRect.bottom - g_savedWindowRect.top) : 660;

    g_main = CreateWindowExW(0, kMainClass, kAppTitle,
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             initialX, initialY, initialW, initialH,
                             nullptr, nullptr, instance, nullptr);
    if (!g_main) {
        MessageBoxW(nullptr, L"Could not create the main player window.", kAppTitle, MB_OK | MB_ICONERROR);
        if (comInitialized) CoUninitialize();
        return 1;
    }

    ApplyDarkTheme(g_main);
    const int initialShow = g_startMaximized ? SW_SHOWMAXIMIZED : showCommand;
    ShowWindow(g_main, initialShow);
    UpdateWindow(g_main);

    // Show the real player window before the potentially slow first libmpv/DLL
    // initialization. Freshly copied binaries may be inspected by Windows or
    // security software on first launch; this makes it clear that the app has
    // started instead of appearing unresponsive.
    g_initializingPlaybackEngine = true;
    if (g_status) SetWindowTextW(g_status, L"Initializing playback engine...");
    if (g_video) {
        InvalidateRect(g_video, nullptr, TRUE);
        UpdateWindow(g_video);
    }
    const bool mpvInitialized = InitMpv();
    g_initializingPlaybackEngine = false;
    if (g_video) InvalidateRect(g_video, nullptr, TRUE);
    if (mpvInitialized) SetTimer(g_main, TIMER_MPV_POLL, 250, nullptr);

    if (!startupPaths.empty()) {
        LoadPlaylist(startupPaths);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_MOUSEWHEEL && GetForegroundWindow() == g_main) {
            AdjustVolumeFromWheel(msg.wParam);
            continue;
        }
        if ((msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) && GetForegroundWindow() == g_main) {
            if (HandleShortcut(msg.wParam)) continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_fileMenu) DestroyMenu(g_fileMenu);
    if (g_viewMenu) DestroyMenu(g_viewMenu);
    if (g_playMenu) DestroyMenu(g_playMenu);
    if (g_helpMenu) DestroyMenu(g_helpMenu);
    if (g_font) DeleteObject(g_font);
    if (g_smallFont) DeleteObject(g_smallFont);
    if (g_panelBrush) DeleteObject(g_panelBrush);
    if (g_editBrush) DeleteObject(g_editBrush);
    if (g_seekBgBrush) DeleteObject(g_seekBgBrush);
    if (g_seekFillBrush) DeleteObject(g_seekFillBrush);
    if (g_chapterMarkerBrush) DeleteObject(g_chapterMarkerBrush);
    if (g_seekPlayheadBrush) DeleteObject(g_seekPlayheadBrush);
    if (comInitialized) CoUninitialize();
    return static_cast<int>(msg.wParam);
}
