// test.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "resource.h"
#include <math.h>

#define MAX_LOADSTRING 100

// ---- Bouncing-ball toy -----------------------------------------------------

#define MAX_BALLS   300
#define TIMER_ID    1
#define TIMER_MS    25          // ~40 frames per second
#define TOPBAR      44          // height of the button strip

// Gravity tuning (pixels per frame^2 and energy kept on each bounce)
#define GRAVITY     0.45f       // downward pull added to dy every frame
#define RESTITUTION 0.80f       // 0..1: how much speed survives a bounce
#define FRICTION    0.98f       // horizontal slowdown while rolling on the floor
#define REST_SPEED  0.6f        // below this, a floored ball stops bouncing

// Child-control command IDs (kept well above the menu IDs)
#define BTN_START   1001
#define BTN_ADD     1002
#define BTN_BURST   1003
#define BTN_CLEAR   1005
#define BTN_GRAVITY 1006

typedef struct
{
	float    x, y;             // centre position
	float    dx, dy;           // velocity, pixels per frame
	int      r;                // radius
	COLORREF color;
} Ball;

// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];								// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];								// The title bar text

static Ball g_balls[MAX_BALLS];
static int  g_ballCount = 0;
static BOOL g_running   = TRUE;
static BOOL g_gravity   = TRUE;
static HWND g_btnStart  = NULL;
static HWND g_btnGravity = NULL;

// Foward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK	About(HWND, UINT, WPARAM, LPARAM);

// ---- Small helpers ---------------------------------------------------------

static void AddBall(int x, int y)
{
	if (g_ballCount >= MAX_BALLS)
		return;

	Ball *b = &g_balls[g_ballCount++];
	float ang = (float)(rand() % 628) / 100.0f;      // 0..2*pi
	float spd = 2.0f + (float)(rand() % 400) / 100.0f;

	b->x   = (float)x;
	b->y   = (float)y;
	b->dx  = (float)cos(ang) * spd;
	b->dy  = (float)sin(ang) * spd;
	b->r   = 6 + rand() % 18;
	b->color = RGB(rand() % 206 + 50, rand() % 206 + 50, rand() % 206 + 50);
}

static void BurstBalls(HWND hWnd)
{
	RECT rc;
	int i;
	GetClientRect(hWnd, &rc);
	for (i = 0; i < 25; i++)
		AddBall(rc.right / 2, (rc.bottom + TOPBAR) / 2);
}

// Advance every ball one frame and bounce it off the play-area walls.
static void StepBalls(HWND hWnd)
{
	RECT rc;
	int i;
	GetClientRect(hWnd, &rc);

	for (i = 0; i < g_ballCount; i++)
	{
		Ball *b = &g_balls[i];

		// Gravity pulls the ball down a little more each frame.
		if (g_gravity)
			b->dy += GRAVITY;

		b->x += b->dx;
		b->y += b->dy;

		// Side walls: reflect and lose a little speed.
		if (b->x - b->r < 0)
		{
			b->x  = (float)b->r;
			b->dx = -b->dx * RESTITUTION;
		}
		if (b->x + b->r > rc.right)
		{
			b->x  = (float)(rc.right - b->r);
			b->dx = -b->dx * RESTITUTION;
		}

		// Ceiling (just under the button strip).
		if (b->y - b->r < TOPBAR)
		{
			b->y  = (float)(TOPBAR + b->r);
			b->dy = -b->dy * RESTITUTION;
		}

		// Floor: bounce with energy loss, add rolling friction, and
		// let a slow ball come to rest instead of jittering forever.
		if (b->y + b->r > rc.bottom)
		{
			b->y  = (float)(rc.bottom - b->r);
			b->dy = -b->dy * RESTITUTION;
			b->dx *= FRICTION;

			if (g_gravity && (float)fabs(b->dy) < REST_SPEED)
				b->dy = 0.0f;
		}
	}
}

// Handle ball-to-ball collisions. For each overlapping pair, push them
// apart so they stop sticking, then exchange momentum along the line
// between their centres. Mass scales with area (radius^2), so a big ball
// shoves a small one more than the other way round.
static void ResolveCollisions(void)
{
	int i, j;

	for (i = 0; i < g_ballCount; i++)
	{
		for (j = i + 1; j < g_ballCount; j++)
		{
			Ball *a = &g_balls[i];
			Ball *b = &g_balls[j];
			float nx = b->x - a->x;
			float ny = b->y - a->y;
			float dist2 = nx * nx + ny * ny;
			float minD  = (float)(a->r + b->r);

			if (dist2 < minD * minD && dist2 > 0.0001f)
			{
				float dist = (float)sqrt(dist2);
				float ux   = nx / dist;            // unit normal, a -> b
				float uy   = ny / dist;
				float overlap = minD - dist;

				float invA = 1.0f / (float)(a->r * a->r);   // 1 / mass
				float invB = 1.0f / (float)(b->r * b->r);
				float invSum = invA + invB;

				float rvx, rvy, vn, jimp;

				// Separate the pair, heavier ball moves less.
				a->x -= ux * overlap * (invA / invSum);
				a->y -= uy * overlap * (invA / invSum);
				b->x += ux * overlap * (invB / invSum);
				b->y += uy * overlap * (invB / invSum);

				// Bounce only if they are moving toward each other.
				rvx = b->dx - a->dx;
				rvy = b->dy - a->dy;
				vn  = rvx * ux + rvy * uy;

				if (vn < 0.0f)
				{
					jimp = -(1.0f + RESTITUTION) * vn / invSum;
					a->dx -= jimp * invA * ux;
					a->dy -= jimp * invA * uy;
					b->dx += jimp * invB * ux;
					b->dy += jimp * invB * uy;
				}
			}
		}
	}
}

// Paint the play area into a memory DC, then blit once (no flicker).
static void PaintPlayArea(HWND hWnd, HDC hdc)
{
	RECT rc;
	HDC  memDC;
	HBITMAP memBmp, oldBmp;
	HBRUSH bg, oldBrush;
	HPEN   oldPen;
	int    i;
	TCHAR  info[128];

	GetClientRect(hWnd, &rc);
	rc.top = TOPBAR;                          // draw below the button strip

	memDC  = CreateCompatibleDC(hdc);
	memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
	oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

	// Background.
	bg = CreateSolidBrush(RGB(18, 18, 28));
	FillRect(memDC, &rc, bg);
	DeleteObject(bg);

	// Balls.
	oldPen = (HPEN)SelectObject(memDC, GetStockObject(NULL_PEN));
	for (i = 0; i < g_ballCount; i++)
	{
		Ball *b = &g_balls[i];
		HBRUSH br = CreateSolidBrush(b->color);
		oldBrush = (HBRUSH)SelectObject(memDC, br);
		Ellipse(memDC,
			(int)(b->x - b->r), (int)(b->y - b->r),
			(int)(b->x + b->r), (int)(b->y + b->r));
		SelectObject(memDC, oldBrush);
		DeleteObject(br);
	}
	SelectObject(memDC, oldPen);

	// Hint text + live count.
	SetBkMode(memDC, TRANSPARENT);
	SetTextColor(memDC, RGB(170, 170, 190));
	wsprintf(info, TEXT("Click anywhere to add a ball   -   balls: %d"), g_ballCount);
	{
		RECT tr = rc;
		tr.top = rc.bottom - 22;
		DrawText(memDC, info, lstrlen(info), &tr, DT_CENTER | DT_SINGLELINE);
	}

	// Blit the finished frame to the screen in one shot.
	BitBlt(hdc, 0, rc.top, rc.right, rc.bottom - rc.top,
		memDC, 0, rc.top, SRCCOPY);

	SelectObject(memDC, oldBmp);
	DeleteObject(memBmp);
	DeleteDC(memDC);
}

static void CreateToolbar(HWND hWnd)
{
	int x = 8;
	int w, h = 28, y = 8;

	struct { int id; LPCTSTR text; int width; } btns[] =
	{
		{ BTN_START,   TEXT("Pause"),       70 },
		{ BTN_ADD,     TEXT("Add Ball"),    80 },
		{ BTN_BURST,   TEXT("Burst!"),      70 },
		{ BTN_GRAVITY, TEXT("Gravity: On"), 90 },
		{ BTN_CLEAR,   TEXT("Clear"),       70 },
	};
	int i;

	for (i = 0; i < 5; i++)
	{
		HWND b;
		w = btns[i].width;
		b = CreateWindow(TEXT("BUTTON"), btns[i].text,
			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			x, y, w, h, hWnd, (HMENU)btns[i].id, hInst, NULL);
		if (btns[i].id == BTN_START)
			g_btnStart = b;
		if (btns[i].id == BTN_GRAVITY)
			g_btnGravity = b;
		x += w + 6;
	}
}

int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR     lpCmdLine,
                     int       nCmdShow)
{
 	// TODO: Place code here.
	MSG msg;
	HACCEL hAccelTable;

	// Initialize global strings
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInstance, IDC_GRAVITYSIM, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance (hInstance, nCmdShow)) 
	{
		return FALSE;
	}

	hAccelTable = LoadAccelerators(hInstance, (LPCTSTR)IDC_GRAVITYSIM);

	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0)) 
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) 
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX); 

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= (WNDPROC)WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, (LPCTSTR)IDI_GRAVITYSIM);
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_BTNFACE+1);
	wcex.lpszMenuName	= (LPCSTR)IDC_GRAVITYSIM;
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(wcex.hInstance, (LPCTSTR)IDI_SMALL);

	return RegisterClassEx(&wcex);
}

//
//   FUNCTION: InitInstance(HANDLE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   HWND hWnd;

   hInst = hInstance; // Store instance handle in our global variable

   hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, 640, 480, NULL, NULL, hInstance, NULL);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, unsigned, WORD, LONG)
//
//  PURPOSE:  Processes messages for the main window.
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message) 
	{
		case WM_CREATE:
			srand(GetTickCount());
			CreateToolbar(hWnd);
			BurstBalls(hWnd);
			SetTimer(hWnd, TIMER_ID, TIMER_MS, NULL);
			break;

		case WM_TIMER:
			if (g_running)
			{
				RECT play;
				StepBalls(hWnd);
				ResolveCollisions();
				GetClientRect(hWnd, &play);
				play.top = TOPBAR;
				InvalidateRect(hWnd, &play, FALSE);   // FALSE: no erase, no flicker
			}
			break;

		case WM_LBUTTONDOWN:
			{
				int x = LOWORD(lParam);
				int y = HIWORD(lParam);
				if (y > TOPBAR)
					AddBall(x, y);
			}
			break;

		case WM_COMMAND:
			wmId    = LOWORD(wParam); 
			wmEvent = HIWORD(wParam); 
			// Parse the menu selections and button clicks:
			switch (wmId)
			{
				case BTN_START:
					g_running = !g_running;
					if (g_btnStart)
						SetWindowText(g_btnStart, g_running ? TEXT("Pause") : TEXT("Resume"));
					break;
				case BTN_ADD:
					{
						RECT rc;
						GetClientRect(hWnd, &rc);
						AddBall(rc.right / 2, (rc.bottom + TOPBAR) / 2);
					}
					break;
				case BTN_BURST:
					BurstBalls(hWnd);
					break;
				case BTN_GRAVITY:
					g_gravity = !g_gravity;
					if (g_btnGravity)
						SetWindowText(g_btnGravity,
							g_gravity ? TEXT("Gravity: On") : TEXT("Gravity: Off"));
					break;
				case BTN_CLEAR:
					g_ballCount = 0;
					InvalidateRect(hWnd, NULL, TRUE);
					break;
				case IDM_ABOUT:
				   DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
				   break;
				case IDM_EXIT:
				   DestroyWindow(hWnd);
				   break;
				default:
				   return DefWindowProc(hWnd, message, wParam, lParam);
			}
			break;
		case WM_PAINT:
			hdc = BeginPaint(hWnd, &ps);
			PaintPlayArea(hWnd, hdc);
			EndPaint(hWnd, &ps);
			break;
		case WM_DESTROY:
			KillTimer(hWnd, TIMER_ID);
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
   }
   return 0;
}

// Mesage handler for about box.
LRESULT CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_INITDIALOG:
				return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
			{
				EndDialog(hDlg, LOWORD(wParam));
				return TRUE;
			}
			break;
	}
    return FALSE;
}
