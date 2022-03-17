#include <iostream>
#include <complex>
#include <limits>
#include <math.h>
#include "fourier.h"
#include "imgui.h"
#include "implot.h"

// System includes
#include <ctype.h>          // toupper
#include <limits.h>         // INT_MIN, INT_MAX
#include <math.h>           // sqrtf, powf, cosf, sinf, floorf, ceilf
#include <stdio.h>          // vsnprintf, sscanf, printf
#include <stdlib.h>         // NULL, malloc, free, atoi
#if defined(_MSC_VER) && _MSC_VER <= 1500 // MSVC 2008 or earlier
#include <stddef.h>         // intptr_t
#else
#include <stdint.h>         // intptr_t
#endif

using namespace std;

// Helpers macros
// We normally try to not use many helpers in imgui_demo.cpp in order to make code easier to copy and paste,
// but making an exception here as those are largely simplifying code...
// In other imgui sources we can use nicer internal functions from imgui_internal.h (ImMin/ImMax) but not in the demo.
#define IM_MIN(A, B)            (((A) < (B)) ? (A) : (B))
#define IM_MAX(A, B)            (((A) >= (B)) ? (A) : (B))
#define IM_CLAMP(V, MN, MX)     ((V) < (MN) ? (MN) : (V) > (MX) ? (MX) : (V))
#define PI 3.14159265
#define TWO_PI 6.28318253f
#define NUM_CYCLES 2.0f
#define GOLDEN_RATIO 1.6180339887 // appearently need to define a square

const char* fourier::concepts[] = { "fourier series",
									"fourier transform (sinusoidal)",
									"demodulate",
};

const char* fourier::strategies[] = { "integers",
										"uneven",
										"even",
										"fibonacci",
										"primes",
										"\"uneven\" primes",
										"\"even\" primes",
										"\"ballanced\" primes",
										"\"emirp\" primes",
										"\"euler\'s irregular\" prime",
										"custom", };
										//"inv. custom",}; broken

const char* fourier::curves[] = { "sin(x)",
								"cos(x)",
								"sin(x)^2 + cos(x) : 2 cycles",
								"cos(x)sin(x) + sin(x)cos(x)",
								"sin(2x)",
								"cos(x)sin(x) - sin(x)",
								"sin(4x) + sin(3x) + sin(2x) + sin(x)",
								"cos(4x) + cos(3x) + cos(2x) + cos(x)",
								"sin(7x) + sin(5x) + sin(3x) + sin(2x) + sin(x)",
								"sin(42x) + sin(13x) + sin(7x) + sin(3x) + sin(x)",
								"saw wave",
								"square wave",
								"square",
								"sin(x)  - sin(2x) + sin(3x) - sin(4x) + sin(5x)....",
								"-sin(x)  + sin(2x) - sin(3x) + sin(4x) - sin(5x)....",
								"dataAnalog[2]",};

ExampleAppConsole fourier::console = {};
ExampleAppLog fourier::log = {};
WaveletGenerator fourier::waveletGenerator = { 64 };
ScrollingBuffer fourier::tracer = { 100000 };
ScrollingBuffer fourier::result = { MAX_NODES };
ScrollingBuffer fourier::dataAnalog[3] = { {MAX_PLOT}, {MAX_PLOT}, };
ScrollingBuffer fourier::dataModulated = { MAX_PLOT };
ScrollingBuffer fourier::demodulator[3] = { {MAX_PLOT}, {MAX_PLOT}, {MAX_PLOT}, };

fourier::fourier()
{
	isDemoWindow = true;
	isPlots = true;
	isDockspace = true;
	isConsole = true;
	isLog = true;
	isAlternateSeries = false;
	time = 0.0f;
	timePlot = 0.0f;
	clear_color = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
	circle_color = ImVec4(0.9f, 0.9f, 0.75f, 1.00f);
	timeChangeRate = 1000.0f;
	plotTimeChangeRate = 1000.0f;
	x = 0.0f;
	y = 0.0f;
	finalX = 0.0f;
	finalY = 0.0f;

	radiusCircle = 64.0f;
	radiusEnd = 4.0f;
	numNodes = 8;
	showCircles = true;
	showEdges = true;
	strategy_current = 0;
	curve_current = 0;
	concept_current = 0;
}

void fourier::ShowGUI()
{
	DrawAppDockSpace(isDockspace);
	DrawProperties();
	switch (concept_current)
	{
	case 0: // fourier series
		DrawPlots(isPlots);
		break;
	case 1: // fourier transform live (sin only)
		DrawPlotsTransformScrolling(isPlots);
		break;
	case 2: // demodulation
		DrawPlotsDemodulate(isPlots);
		break;
	}
	DrawCanvas();
	DrawConsole(isConsole);
	DrawLog(isLog);
}

void fourier::Init()
{
	waveletGenerator.Clear();
	waveletGenerator.SetRadius(radiusCircle);
	Setup();
}

void fourier::DrawCanvas()
{
	ImGui::Begin("Canvas");
	//waveletGenerator.Clear();
//	waveletGenerator.SetRadius(radiusCircle);

//	Setup();

	// begin canvas
	static ImVector<ImVec2> points;
	static ImVec2 scrolling(0.0f, 0.0f);
	static bool opt_enable_grid = true;
	static bool opt_enable_context_menu = true;
	static bool adding_line = false;

	ImGui::Checkbox("Enable grid", &opt_enable_grid); ImGui::SameLine();
	ImGui::Checkbox("Enable context menu", &opt_enable_context_menu);
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS) : Frequency %.3f Hz", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate, waveletGenerator.GetFrequency());
	//ImGui::Text("Mouse Left: drag to add lines,\nMouse Right: drag to scroll, click for context menu.");

	// Typically you would use a BeginChild()/EndChild() pair to benefit from a clipping region + own scrolling.
	// Here we demonstrate that this can be replaced by simple offsetting + custom drawing + PushClipRect/PopClipRect() calls.
	// To use a child window instead we could use, e.g:
	//      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));      // Disable padding
	//      ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(50, 50, 50, 255));  // Set a background color
	//      ImGui::BeginChild("canvas", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NoMove);
	//      ImGui::PopStyleColor();
	//      ImGui::PopStyleVar();
	//      [...]
	//      ImGui::EndChild();

	// Using InvisibleButton() as a convenience 1) it will advance the layout cursor and 2) allows us to use IsItemHovered()/IsItemActive()
	ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();      // ImDrawList API uses screen coordinates!
	ImVec2 canvas_sz = ImGui::GetContentRegionAvail();   // Resize canvas to what's available
	if (canvas_sz.x < 50.0f) canvas_sz.x = 50.0f;
	if (canvas_sz.y < 50.0f) canvas_sz.y = 50.0f;
	ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

	// Draw border and background color
	ImGuiIO& io = ImGui::GetIO();

	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(clear_color.x * 255, clear_color.y * 255, clear_color.z * 255, 255));
	draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(255, 255, 255, 255));

	// This will catch our interactions
	ImGui::InvisibleButton("canvas", canvas_sz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool is_hovered = ImGui::IsItemHovered(); // Hovered
	const bool is_active = ImGui::IsItemActive();   // Held
	const ImVec2 origin(canvas_p0.x + scrolling.x, canvas_p0.y + scrolling.y); // Lock scrolled origin
	const ImVec2 mouse_pos_in_canvas(io.MousePos.x - origin.x, io.MousePos.y - origin.y);

	//// Add first and second point
	if (is_hovered && !adding_line && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		points.push_back(mouse_pos_in_canvas);
		// need to add temporary point that will be replaced in the next if statement by the most current mouse cursor's position
		points.push_back(mouse_pos_in_canvas);
		adding_line = true;
	}

	if (adding_line)
	{
		points.back() = mouse_pos_in_canvas;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			adding_line = false;
	}

	// Pan (we use a zero mouse threshold when there's no context menu)
	// You may decide to make that threshold dynamic based on whether the mouse is hovering something etc.
	const float mouse_threshold_for_pan = opt_enable_context_menu ? -1.0f : 0.0f;
	if (is_active && ImGui::IsMouseDragging(ImGuiMouseButton_Right, mouse_threshold_for_pan))
	{
		scrolling.x += io.MouseDelta.x;
		scrolling.y += io.MouseDelta.y;
	}

	// Context menu (under default mouse threshold)
	ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
	if (opt_enable_context_menu && drag_delta.x == 0.0f && drag_delta.y == 0.0f)
		ImGui::OpenPopupOnItemClick("context", ImGuiPopupFlags_MouseButtonRight);
	if (ImGui::BeginPopup("context"))
	{
		if (adding_line)
			points.resize(points.size() - 2);
		adding_line = false;
		if (ImGui::MenuItem("Remove one", NULL, false, points.Size > 0)) { points.resize(points.size() - 2); }
		if (ImGui::MenuItem("Remove all", NULL, false, points.Size > 0)) { points.clear(); }
		ImGui::EndPopup();
	}

	// Draw grid + all lines in the canvas
	draw_list->PushClipRect(canvas_p0, canvas_p1, true);
	const float GRID_STEP = 64.0f;
	if (opt_enable_grid)
	{
		const float GRID_OFFSET_X = fmodf(canvas_sz.x / 2.0f, GRID_STEP);
		const float GRID_OFFSET_Y = fmodf(canvas_sz.y / 2.0f, GRID_STEP);

		for (float x = fmodf(scrolling.x, GRID_STEP) + GRID_OFFSET_X; x < canvas_sz.x; x += GRID_STEP)
			draw_list->AddLine(ImVec2(canvas_p0.x + x, canvas_p0.y), ImVec2(canvas_p0.x + x, canvas_p1.y), IM_COL32(200, 200, 200, 40));
		for (float y = fmodf(scrolling.y, GRID_STEP) + GRID_OFFSET_Y; y < canvas_sz.y; y += GRID_STEP)
			draw_list->AddLine(ImVec2(canvas_p0.x, canvas_p0.y + y), ImVec2(canvas_p1.x, canvas_p0.y + y), IM_COL32(200, 200, 200, 40));
	}
	for (int n = 0; n < points.Size; n += 2)
		draw_list->AddLine(ImVec2(origin.x + points[n].x, origin.y + points[n].y), ImVec2(origin.x + points[n + 1].x, origin.y + points[n + 1].y), IM_COL32(255, 255, 0, 255), 2.0f);

	ImVec2 circle_pos = ImVec2(canvas_p0.x + (canvas_sz.x / 2) + scrolling.x, canvas_p0.y + (canvas_sz.y / 2) + scrolling.y);
	ImVec2 tip;

	switch (concept_current) {
	case 0: // fourier series
		waveletGenerator.DrawWavelets(draw_list, (double)time, circle_pos, showCircles, showEdges);
		waveletGenerator.DrawTraceLine(draw_list, circle_pos, showEdges);

		finalX = -waveletGenerator.GetFinalTip().x / waveletGenerator.GetNormalizer();
		finalY = -waveletGenerator.GetFinalTip().y / waveletGenerator.GetNormalizer();
		break;
	case 1: // wind data based on live ticks
		waveletGenerator.DrawWavelet(draw_list, 0, time, finalY, circle_pos, showCircles, showEdges);
		waveletGenerator.DrawTraceLine(draw_list, circle_pos, showEdges);
		break;
	case 2: // wind data for demodulation 
		waveletGenerator.DrawWavelet(log, draw_list, plotTimeChangeRate, 0, dataModulated, demodulator, result, numNodes, circle_pos, showCircles, showEdges);
		break;
	}

	time += PI / timeChangeRate;
	if (time >= TWO_PI)
		time = 0.0f;
	if (concept_current < 2)
	{

		if (tracer.Data.Size > 0)
			tracer.AddPoint(waveletGenerator.GetFinalTip().x, waveletGenerator.GetFinalTip().y);
		else
			tracer.AddPoint(0.0f, 0.0f);

		for (int i = 0; i < tracer.Data.size(); i++)
		{
			ImVec2 p = ImVec2(tracer.Data[i].x + circle_pos.x, tracer.Data[i].y + circle_pos.y);
			if (i == 0)
			{
				draw_list->AddCircle(p, 5.0f, IM_COL32(255, 20, 125, 255), 0, 2.0f);
			}
			else
			{
				draw_list->AddCircle(p, 1.0f, IM_COL32(20, 125, 225, 255), 0, 1.0f);
			}
		}

		ImVec2 p = ImVec2(waveletGenerator.GetFinalTip().x + circle_pos.x, waveletGenerator.GetFinalTip().y + circle_pos.y);
		draw_list->AddCircle(p, 3.0f, IM_COL32(255, 20, 125, 255), 0, 2.0f);
	}

	draw_list->AddCircle(circle_pos, 3.0f, IM_COL32(255, 20, 125, 255), 0, 2.0f);
	ImVec2 pog = ImVec2(waveletGenerator.GetPog().x + circle_pos.x,
		waveletGenerator.GetPog().y + circle_pos.y);
	draw_list->AddCircle(pog, 10.0f, IM_COL32(200, 200, 80, 255), 0, 2.0f);

	draw_list->PopClipRect();
	// end canvas
	ImGui::End();

}

void fourier::DrawProperties()
{
	static double f = 0.0f;
	static int counter = 0;
	bool updateRequired = false;

	// Create a window called "Properties" and append into it.
	ImGui::Begin("Properties");
	ImGui::Checkbox("Draw Circles", &showCircles); ImGui::SameLine();
	ImGui::Checkbox("Draw Edges", &showEdges);
	if (concept_current == 0)
	{
		ImGui::SameLine();
		if (ImGui::Checkbox("Use Alternate Series", &isAlternateSeries))
		{
			waveletGenerator.EnableAlternateSeries(isAlternateSeries);
			updateRequired = true;
		}
	}

	ImGui::Separator();
	updateRequired = ImGui::ListBox("Concepts", &concept_current, concepts, IM_ARRAYSIZE(concepts), 3) || updateRequired;
	ImGui::Separator();
	bool isTransform = concept_current > 0;

	updateRequired = !isTransform && ImGui::ListBox("Strategy", &strategy_current, strategies, IM_ARRAYSIZE(strategies), 6) || updateRequired;

	bool changed_curve = false;
	if (concept_current == 2) {
		changed_curve = ImGui::ListBox("Curve", &curve_current, curves, IM_ARRAYSIZE(curves), 3);
	}
	updateRequired = changed_curve || updateRequired;
	ImGui::Separator();

	//if (strategy_current < 4 && concept_current != 1) // primes have set number of nodes, ie slider does not do anything for primes series
	updateRequired = ImGui::SliderInt("Num of Nodes", &numNodes, 1, MAX_NODES) || updateRequired;

	//if (concept_current != 2) // primes have set number of nodes, ie slider does not do anything for primes series
	updateRequired = ImGui::SliderFloat("Slowmo Rate Canvas", &timeChangeRate, 10.0f, 10000.0f) || updateRequired;
	updateRequired = ImGui::SliderFloat("Slowmo Rate Plot", &plotTimeChangeRate, 10.0f, 10000.0f) || updateRequired;
	updateRequired = ImGui::SliderFloat("Radius", &radiusCircle, 32.0f, 512.0f /*65536.0f*/) || updateRequired;
	ImGui::Separator();

	ImGui::ColorEdit3("Clear Color", (float*)&clear_color); // Edit 3 floats representing a color
	ImGui::ColorEdit3("Draw Color", (float*)&circle_color); // Edit 3 floats representing a color
	ImGui::Separator();
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::Text("Time %.3f", time);
	ImGui::End();
	if (updateRequired)
	{
		Clear();
		waveletGenerator.Clear();
		demodulator[0].Erase();
		demodulator[1].Erase();
		demodulator[2].Erase();
		timePlot = 0.0f;
		waveletGenerator.SetRadius(radiusCircle);
		Setup();
		if (concept_current != 0)// || (concept_current == 0 && strategy_current == 10)) // this is busted TODO
		{
			result.Erase();
		}

		if(concept_current != 2)
			dataAnalog[2].Erase(); // holds temporary data for further analysis that needs to be removed now



		log.AddLog("[%.1f] - strategy: %d - nodes: %d  - slomo rate: %.1f - radius: %.1f - alternate series: %s\n",
			ImGui::GetTime(), strategy_current, numNodes, timeChangeRate, radiusCircle, isAlternateSeries ? "true" : "false");
	}
}

void fourier::DrawAppDockSpace(bool& open)
{
	// If you strip some features of, this demo is pretty much equivalent to calling DockSpaceOverViewport()!
	// In most cases you should be able to just call DockSpaceOverViewport() and ignore all the code below!
	// In this specific demo, we are not using DockSpaceOverViewport() because:
	// - we allow the host window to be floating/moveable instead of filling the viewport (when opt_fullscreen == false)
	// - we allow the host window to have padding (when opt_padding == true)
	// - we have a local menu bar in the host window (vs. you could use BeginMainMenuBar() + DockSpaceOverViewport() in your code!)
	// TL;DR; this demo is more complicated than what you would normally use.
	// If we removed all the options we are showcasing, this demo would become:
	//     void ShowExampleAppDockSpace()
	//     {
	//         ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
	//     }

	static bool opt_fullscreen = true;
	static bool opt_padding = false;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

	// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
	// because it would be confusing to have two docking targets within each others.
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	if (opt_fullscreen)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	}
	else
	{
		dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
	}

	// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
	// and handle the pass-thru hole, so we ask Begin() to not render a background.
	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;

	// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
	// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
	// all active windows docked into it will lose their parent and become undocked.
	// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
	// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
	if (!opt_padding)
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace Demo", &open, window_flags);
	if (!opt_padding)
		ImGui::PopStyleVar();

	if (opt_fullscreen)
		ImGui::PopStyleVar(2);

	// Submit the DockSpace
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}
	else
	{
		//ShowDockingDisabledMessage();
	}

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Options"))
		{
			// Disabling fullscreen would allow the window to be moved to the front of other windows,
			// which we can't undo at the moment without finer window depth/z control.
			ImGui::MenuItem("Fullscreen", NULL, &opt_fullscreen);
			ImGui::MenuItem("Padding", NULL, &opt_padding);
			ImGui::Separator();

			if (ImGui::MenuItem("Flag: NoSplit", "", (dockspace_flags & ImGuiDockNodeFlags_NoSplit) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_NoSplit; }
			if (ImGui::MenuItem("Flag: NoResize", "", (dockspace_flags & ImGuiDockNodeFlags_NoResize) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_NoResize; }
			if (ImGui::MenuItem("Flag: NoDockingInCentralNode", "", (dockspace_flags & ImGuiDockNodeFlags_NoDockingInCentralNode) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_NoDockingInCentralNode; }
			if (ImGui::MenuItem("Flag: AutoHideTabBar", "", (dockspace_flags & ImGuiDockNodeFlags_AutoHideTabBar) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_AutoHideTabBar; }
			if (ImGui::MenuItem("Flag: PassthruCentralNode", "", (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode) != 0, opt_fullscreen)) { dockspace_flags ^= ImGuiDockNodeFlags_PassthruCentralNode; }
			ImGui::Separator();

			if (ImGui::MenuItem("Close", NULL, false, open))
				open = false;
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	ImGui::End();
}

void fourier::DrawConsole(bool& open)
{
	console.Draw("Console", &open);
}

void fourier::DrawLog(bool& open)
{
	// For the demo: add a debug button _BEFORE_ the normal log window contents
	// We take advantage of a rarely used feature: multiple calls to Begin()/End() are appending to the _same_ window.
	// Most of the contents of the window will be added by the log.Draw() call.
	ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
	ImGui::Begin("Log", &open);
	if (ImGui::SmallButton("[Debug] Add 5 entries"))
	{
		static int counter = 0;
		const char* categories[3] = { "info", "warn", "error" };
		const char* words[] = { "Bumfuzzled", "Cattywampus", "Snickersnee", "Abibliophobia", "Absquatulate", "Nincompoop", "Pauciloquent" };
		for (int n = 0; n < 5; n++)
		{
			const char* category = categories[counter % IM_ARRAYSIZE(categories)];
			const char* word = words[counter % IM_ARRAYSIZE(words)];
			log.AddLog("[%05d] [%s] Hello, current time is %.1f, here's a word: '%s'\n",
				ImGui::GetFrameCount(), category, ImGui::GetTime(), word);
			counter++;
		}
	}
	ImGui::End();

	// Actually call in the regular Log helper (which will Begin() into the same window as we just did)
	log.Draw("Log", &open);
}

void fourier::DrawPlotsDemodulate(bool& open)
{
	ImGui::Begin("DigitalPlots", &open);
	static bool showAnalog[3] = { true, true, true };
	float x = 0.0f;
	char label[32];
	ImGui::Checkbox("cos(x)", &showAnalog[0]); ImGui::SameLine();
	ImGui::Checkbox("sin(x)", &showAnalog[1]); ImGui::SameLine();
	ImGui::Checkbox("magnitude", &showAnalog[2]);

	float range = TWO_PI;
	float minY = 100.0f;
	float maxY = 0.0f;

	finalY = 0.0f;
	finalX = 0.0f;
	double tmp = 0.0f;
	double rDiv2 = 1.0f;

	for (int i = 0; i < dataModulated.MaxSize; i++) {
		switch (curve_current) {
		case 0: // cos(x)
			finalY = sin(x);
			break;
		case 1: // cos(x)
			finalY = cos(x);
			break;
		case 2: //sin(x)^2 + cos(x) : 2 cycles
			finalY = (((sin(x) * sin(x)) + cos(x)));
			range = TWO_PI * NUM_CYCLES;
			break;
		case 3: // cos(x)sin(x)+sin(x)cos(x)
			finalY = (cos(x) * sin(x)) + (sin(x) * cos(x));
			break;
		case 4: //"sin(2x)",
			finalY = sin(2 * x);
			break;
		case 5: //cos(x)sin(x) - sin(x)
			finalY = (cos(x) * sin(x)) - sin(x);
			break;
		case 6: //"4sin(x) + 3sin(x) + 2sin(x) + sin()"
			finalY = sin(4 * x) + sin(3 * x) + sin(2 * x) + sin(x);
			break;
		case 7: //"cos(4x) + cos(3x) + cos(2x) + cos()",
			finalY = cos(4 * x) + cos(3 * x) + cos(2 * x) + cos(x);
			break;
		case 8: // sin(7x) + sin(5x) + sin(3x) + sin(2x) + sin()
			finalY = sin(7 * x) + sin(5 * x) + sin(3 * x) + sin(2 * x) + sin(x);
			break;
		case 9: // "sin(42x) + sin(13x) + sin(7x) + sin(3x) + sin()",
			finalY = sin(42 * x) + sin(13 * x) + sin(7 * x) + sin(3 * x) + sin(x);
			break;
		case 10: // sum(sin(2x)) : even integers
			finalY = 0.0f;
			for (int i = 1; i < (numNodes + 1); i++)
			{
				tmp = (i * 2.0f);
				finalY += (4 * sin(x * tmp)) / (PI * tmp);
			}

			break;
		case 11: // sum(sin(2x - 1)) : odd integers / square wave
			finalY = 0.0f;
			for (int i = 1; i < (numNodes + 1); i++)
			{
				tmp = (i * 2.0f) - 1.0f;
				finalY += (4 * sin(x * tmp)) / (PI * tmp);
			}
			break;
		case 12: // square
			rDiv2 = HALF_LEN;

			if (x <= 0.0f)
				finalY = rDiv2;
			else if (x < PI / 4.0f)
				finalY = rDiv2 / cos(x);
			else if (x == PI / 4.0f)
				finalY = sqrt(2 * rDiv2);
			else if (x < PI / 2.0f)
				finalY = rDiv2 / sin(x);
			else if (x == PI / 2.0f)
				finalY = rDiv2;
			else if (x < 3 * PI / 4.0f)
				finalY = rDiv2 / sin(x);
			else if (x == 3 * PI / 4.0f)
				finalY = sqrt(2 * rDiv2);
			else if (x < PI)
				finalY = -rDiv2 / cos(x);
			else if (x == PI)
				finalY = rDiv2;
			else if (x < 5 * PI / 4.0f)
				finalY = -rDiv2 / cos(x);
			else if (x == 5 * PI / 4.0f)
				finalY = sqrt(2 * rDiv2);
			else if (x == 3.0f * PI / 2.0f)
				finalY = rDiv2;
			else if (x < 7 * PI / 4.0f)
				finalY = -rDiv2 / sin(x);
			else if (x == 7 * PI / 4.0f)
				finalY = sqrt(2 * rDiv2);
			else if (x < TWO_PI)
				finalY = rDiv2 / cos(x);
			else if (x >= TWO_PI)
				finalY = rDiv2;

			assert(finalY < (2 * rDiv2));
			break;
		case 13://"sin(x)  - sin(2x) + sin(3x) - sin(4x) + sin(5x)....",
			finalY = 0.0f;
			for (int i = 1; i < (numNodes + 1); i++)
			{
				tmp = i;
				finalY += i % 2 ? sin(x * tmp) : -sin(x * tmp);
			}
			break;
		case 14://	"-sin(x)  + sin(2x) - sin(3x) + sin(4x) - sin(5x)....",
			finalY = 0.0f;
			for (int i = 1; i < (numNodes + 1); i++)
			{
				tmp = i;
				finalY += !(i % 2) ? sin(x * tmp) : -sin(x * tmp);
			}
			break;
		case 15://"dataAnalog[2]"
			if(dataAnalog[2].Data.size()>i)
				finalY = dataAnalog[2].Data[i].y;
				//dataModulated.AddPoint(dataAnalog[2].Data[i].x, dataAnalog[2].Data[i].y);
			break;
		}

		//if(curve_current != 15)
		//{
			minY = IM_MIN(minY, finalY);
			maxY = IM_MAX(maxY, finalY);

			x += range / dataModulated.MaxSize;

			dataModulated.AddPoint(x, finalY);
		//}
	}

	ImVec2 region = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y / 2.0f);
	if (ImPlot::BeginPlot("##Digital", region)) {
		ImPlot::SetupAxisLimits(ImAxis_X1, 0, range, ImGuiCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, minY - 0.5f, maxY + 0.5f);
		strcpy_s(label, 32, "Curve");
		if (dataModulated.Data.size() > 0)
			ImPlot::PlotLine(label, &dataModulated.Data[0].x, &dataModulated.Data[0].y, dataModulated.Data.size(), dataModulated.Offset, 2 * sizeof(float));
		ImPlot::EndPlot();
	}

	timePlot += (numNodes * TWO_PI / plotTimeChangeRate);

	if (ImPlot::BeginPlot("##Demodulate", ImGui::GetContentRegionAvail())) {
		ImPlot::SetupAxisLimits(ImAxis_X1, 0.0f, timePlot, waveletGenerator.Pause() ? ImGuiCond_Once : ImGuiCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, -1.875, 1.875);

		if (showAnalog[0])
		{
			strcpy_s(label, 32, "cos(x)");
			if (demodulator[0].Data.size() > 0)
				ImPlot::PlotLine(label, &demodulator[0].Data[0].x, &demodulator[0].Data[0].y, demodulator[0].Data.size(), demodulator[0].Offset, 2 * sizeof(float));
		}
		if (showAnalog[1])
		{
			strcpy_s(label, 32, "sin(x)");
			if (demodulator[1].Data.size() > 0)
				ImPlot::PlotLine(label, &demodulator[1].Data[0].x, &demodulator[1].Data[0].y, demodulator[1].Data.size(), demodulator[1].Offset, 2 * sizeof(float));
		}
		if (showAnalog[2])
		{
			strcpy_s(label, 32, "magnitude");
			if (demodulator[2].Data.size() > 0)
				ImPlot::PlotLine(label, &demodulator[2].Data[0].x, &demodulator[2].Data[0].y, demodulator[1].Data.size(), demodulator[2].Offset, 2 * sizeof(float));
		}
		ImPlot::EndPlot();
	}
	ImGui::End();
}

void fourier::DrawPlotsTransformScrolling(bool& open) {
	ImGui::Begin("DigitalPlots", &open);

	static bool paused = false;
	static bool showAnalog[2] = { true, true };
	static bool flipSign = false;
	static double prevX = 1.0f;

	char label[32];
	ImGui::Checkbox("cos(x)", &showAnalog[0]);  ImGui::SameLine();
	ImGui::Checkbox("sin(x)", &showAnalog[1]);

	switch (curve_current) {
	case 0: // cos(x)
		finalY = (float)sin(timePlot);
		finalX = (float)cos(timePlot);
		break;
	case 1: // cos(x)
		finalY = (float)cos(timePlot);
		finalX = (float)-sin(timePlot);
		break;
	case 2: //sin(x)^2 + cos(x)
		finalY = (sin(timePlot) * sin(timePlot)) + cos(timePlot);
		finalX = 2 * (sin(timePlot) * cos(timePlot)) - sin(timePlot);
		break;
	case 3: // cos(x)sin(x)+sin(x)cos(x)
		finalY = (cos(timePlot) * sin(timePlot)) + (sin(timePlot) * cos(timePlot));
		finalX = (-sin(timePlot) * sin(timePlot)) + (cos(timePlot) * cos(timePlot)) + (-sin(timePlot) * sin(timePlot)) + (cos(timePlot) * cos(timePlot));
		break;
	case 4: //"sin(2x)",
		finalY = sin(2 * timePlot);
		finalX = 2 * cos(2 * timePlot);
		break;
	case 5: //cos(x)sin(x) - sin(x)
		finalY = (cos(timePlot) * sin(timePlot)) - sin(timePlot);
		finalX = (-sin(timePlot) * sin(timePlot)) + (cos(timePlot) * cos(timePlot)) - cos(timePlot);
		break;
	case 6: //"4sin(x) + 3sin(x) + 2sin(x) + sin()"
		finalY = sin(4 * timePlot) + sin(3 * timePlot) + sin(2 * timePlot) + sin(timePlot);
		finalX = 4 * cos(4 * timePlot) + 3 * cos(3 * timePlot) + 2 * cos(2 * timePlot) + cos(timePlot);
		break;
	case 7: //"cos(4x) + cos(3x) + cos(2x) + cos()",
		finalY = cos(4 * timePlot) + cos(3 * timePlot) + cos(2 * timePlot) + cos(timePlot);
		finalX = -4 * sin(4 * timePlot) - 3 * sin(3 * timePlot) - 2 * sin(2 * timePlot) - sin(timePlot);
		break;
	case 8: // sin(7x) + sin(5x) + sin(3x) + sin(2x) + sin()
		finalY = sin(7 * timePlot) + sin(5 * timePlot) + sin(3 * timePlot) + sin(2 * timePlot) + sin(timePlot);
		finalX = 7 * cos(7 * timePlot) + 5 * cos(5 * timePlot) + 3 * cos(3 * timePlot) * 2 * cos(2 * timePlot) + cos(timePlot);
		break;
	case 9: // "sin(42x) + sin(13x) + sin(7x) + sin(3x) + sin()",
		finalY = sin(42 * timePlot) + sin(13 * timePlot) + sin(7 * timePlot) + sin(3 * timePlot) + sin(timePlot);
		finalX = 42 * cos(42 * timePlot) + 13 * cos(13 * timePlot) + 5 * cos(5 * timePlot) * 3 * cos(3 * timePlot) + cos(timePlot);
		break;
	}

	//std::complex<double> comp = sqrt(std::complex<double>(1.0f - (finalY * finalY), 0));
	//log.AddLog("%.2f + i%.2f\n", comp.real(), comp.imag());

	if (!paused) {
		timePlot += (PI / plotTimeChangeRate); //ImGui::GetIO().DeltaTime;
		if (showAnalog[0])
			dataAnalog[0].AddPoint((float)(-timePlot), (float)finalX);
		if (showAnalog[1])
			dataAnalog[1].AddPoint((float)(-timePlot), (float)finalY);
	}

	if (ImPlot::BeginPlot("##Digital", ImGui::GetContentRegionAvail())) {
		ImPlot::SetupAxisLimits(ImAxis_X1, -timePlot + 10.0, -timePlot, paused ? ImGuiCond_Once : ImGuiCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, -1.875, 1.875);
		for (int i = 0; i < 2; ++i) {
			if (showAnalog[i]) {
				strcpy_s(label, 32, i ? "sin(x)" : "cos(x)");
				if (dataAnalog[i].Data.size() > 0)
					ImPlot::PlotLine(label, &dataAnalog[i].Data[0].x, &dataAnalog[i].Data[0].y, dataAnalog[i].Data.size(), dataAnalog[i].Offset, 2 * sizeof(float));
			}
		}
		ImPlot::EndPlot();
	}
	ImGui::End();
}

void fourier::DrawPlots(bool& p_open) {
	ImGui::Begin("DigitalPlots", &p_open);

	static bool paused = false;
	static bool showAnalog[2] = { true, true };

	char label[32];
	ImGui::Checkbox("cos(x)", &showAnalog[0]);  ImGui::SameLine();
	ImGui::Checkbox("sin(x)", &showAnalog[1]);

	if (!paused) {
		timePlot += (PI / plotTimeChangeRate); //ImGui::GetIO().DeltaTime;
		if (showAnalog[0])
			dataAnalog[0].AddPoint((float)(-timePlot), (float)finalX);
		if (showAnalog[1])
			dataAnalog[1].AddPoint((float)(-timePlot), (float)finalY);
	}

	if ((concept_current != 2) && !(strategy_current == 10 && concept_current == 0))
	{
		dataAnalog[2].AddPoint((float)(-timePlot), (float)finalX);
		log.AddLog("adding %.3f:%.3f to cache\n", timePlot, finalX);
	}

	if (ImPlot::BeginPlot("##Digital", ImGui::GetContentRegionAvail())) {
		ImPlot::SetupAxisLimits(ImAxis_X1, -timePlot + 10.0, -timePlot, paused ? ImGuiCond_Once : ImGuiCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, -1.875, 1.875);
		for (int i = 0; i < 2; ++i) {
			if (showAnalog[i]) {
				strcpy_s(label, 32, i ? "sin(x)" : "cos(x)");
				if (dataAnalog[i].Data.size() > 0)
					ImPlot::PlotLine(label, &dataAnalog[i].Data[0].x, &dataAnalog[i].Data[0].y, dataAnalog[i].Data.size(), dataAnalog[i].Offset, 2 * sizeof(float));
			}
		}
		ImPlot::EndPlot();
	}
	ImGui::End();
}

void fourier::SetupSingleWavelet()
{
	waveletGenerator.AddWavelet(1, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
}

void fourier::SetupMulitpleWavelets()
{
	int fiba = 1;
	int nacho = 1;

	switch (strategy_current)
	{
	case 0:
		// uneven
		for (int i = 0; i < numNodes; i++)
		{
			waveletGenerator.AddWavelet((i + 1), IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255)); // add uneven indecies
		}
		break;
	case 1:
		// uneven
		for (int i = 0; i < numNodes; i++)
		{
			waveletGenerator.AddWavelet(((i + 1) * 2) - 1, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255)); // add uneven indecies
		}
		break;
	case 2:
		//even
		for (int i = 0; i < numNodes; i++)
		{
			waveletGenerator.AddWavelet(((i + 1) * 2), IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255)); // add even indecies
		}
		break;
	case 3:
		//fibonacci
		for (int i = 0; i < numNodes; i++)
		{
			waveletGenerator.AddWavelet(nacho, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255)); // add all indecies
			nacho += fiba;
			fiba = nacho;
		}
		break;
	case 4:
		// primary numbers
		waveletGenerator.AddWavelet(2, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(3, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(5, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(7, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(11, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(13, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(17, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(19, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(23, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(29, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(31, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(37, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(41, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(43, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(47, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(53, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(59, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(61, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(67, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(71, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(73, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(79, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(83, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(89, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(97, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(101, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(103, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(107, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(109, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(113, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(127, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(131, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(137, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(139, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(149, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(151, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(157, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(163, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(167, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(173, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(179, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(181, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(191, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(193, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(197, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(199, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(211, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(223, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(227, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(229, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		break;
	case 5:
		// "uneven" primary numbers
		waveletGenerator.AddWavelet(3, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(7, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(13, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(19, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(29, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(37, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(43, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(53, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(61, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(71, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(79, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(89, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(101, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(107, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(113, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(131, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(139, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(151, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(163, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(173, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(181, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(193, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(199, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(223, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		break;
	case 6:
		// "even" primary numbers
		waveletGenerator.AddWavelet(2, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(5, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(11, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(17, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(23, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(31, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(41, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(47, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(59, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(67, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(73, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(83, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(97, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(103, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(109, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(127, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(137, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(149, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(157, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(167, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(179, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(191, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(197, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(211, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		break;
	case 7:
		// "ballanced" primary numbers
		waveletGenerator.AddWavelet(5, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(53, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(157, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(173, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(211, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(257, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(263, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(373, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(563, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		break;
	case 8:
		// "emirps" primary numbers
		waveletGenerator.AddWavelet(13, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(17, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(31, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(37, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(71, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(73, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(79, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(97, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(107, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(113, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(149, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(157, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(167, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(179, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(199, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		break;
	case 9:
		// "euler irregular" primary numbers
		waveletGenerator.AddWavelet(19, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(31, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(43, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(47, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(61, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(67, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(71, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(79, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(101, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(137, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(139, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(149, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(193, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(223, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(241, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(251, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(263, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(277, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(307, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(311, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(349, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(353, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(359, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(373, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(379, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(419, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(433, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(461, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(463, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(491, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(509, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(541, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(563, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(571, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(577, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		waveletGenerator.AddWavelet(587, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		break;
	case 10: // custom use result buffer if it contains data
		if (result.Data.size() > 0)
		{
			for (int i = 0; i < result.Data.size(); i++)
			{
				waveletGenerator.AddWavelet((result.Data[i].x * radiusCircle), (result.Data[i].y * radiusCircle));
			}
		}
		break;
	}
}

void fourier::Clear()
{
	tracer.Erase();
	dataModulated.Erase();
	dataAnalog[0].Erase();
	dataAnalog[1].Erase();
	//dataAnalog[2].Erase(); // needs to be cleared at different special times
	finalY = 0.0f;
	finalX = 0.0f;
}

void fourier::Setup()
{
	if (concept_current == 0) // fourier series
		SetupMulitpleWavelets();
	else // demodulation and fourier transform
		SetupSingleWavelet();
}

WaveletGenerator::WaveletGenerator(double radius)
{
	this->radius = radius;
	this->normalizer = 0;
	this->finalTip = ImVec2(0.0f, 0.0f);
	this->useAlternateSeries = false;
	this->range = 0.0f;
	this->maxRange = MAX_FREQUENCY * TWO_PI;
	this->pauseDemodulator = false;
}

WaveletGenerator::~WaveletGenerator()
{
	Clear();
}

void WaveletGenerator::Clear()
{
	for (int i = 0; i < waveletQueue.size(); i++)
	{
		if (waveletQueue[i])
			delete(waveletQueue[i]);
	}
	waveletQueue.clear();
	normalizer = 0;
	range = 0.0f;
	maxRange = MAX_FREQUENCY * TWO_PI;
	pauseDemodulator = false;
}



void WaveletGenerator::Rotate(int i, double t)
{
	// TODO: need to see if I can switch sin and cos somehow in other words alternate back an forth on x only or y only!?! dunno if that makes sense
	// the idea is that the series contains a summ of cosine as well as a sum of sine tokens. This seems to be relevant for the square problem.
	waveletQueue[i]->rotation = ImVec2((float)(waveletQueue[i]->radius * (double)cos(-t * waveletQueue[i]->index)), (float)(waveletQueue[i]->radius * (double)sin(-t * waveletQueue[i]->index)));
	
	if (i > 0)
		waveletQueue[i]->tail = ImVec2(waveletQueue[i - 1]->tip.x, waveletQueue[i - 1]->tip.y);
	waveletQueue[i]->tip = ImVec2(waveletQueue[i]->tail.x + waveletQueue[i]->rotation.x, waveletQueue[i]->tail.y + waveletQueue[i]->rotation.y);
}

// will draw a full data set wound arround the wavelet numOfTimes times
void WaveletGenerator::DrawWavelet(ExampleAppLog& log,
	ImDrawList* draw_list,
	float& plotTimeChangeRate,
	int index,
	ScrollingBuffer& curve,
	ScrollingBuffer* output,
	ScrollingBuffer& result,
	int numOfTimes,
	ImVec2 origin,
	bool drawCircles,
	bool drawEdges)
{
	double factor = 1.0f;
	double stepFrequency = (numOfTimes * TWO_PI / plotTimeChangeRate);
	static bool isPositive = false;

	this->maxRange = numOfTimes + 1.0f;

	waveletQueue[index]->numCoords = 0;
	waveletQueue[index]->totX = 0.0f;
	waveletQueue[index]->totY = 0.0f;

	double rotation = 0.0f;
	double rotationStep = (TWO_PI * this->range) / curve.Data.size();
	ImVec2 cog;

	for (int i = 0; i < curve.Data.Size; i++)
	{
		factor = curve.Data[i].y; // here is a tricky problem
		rotation += rotationStep;
		Rotate(index, -rotation);

		ImVec2 center = ImVec2((float)(waveletQueue[index]->tail.x + origin.x), (float)(waveletQueue[index]->tail.y + origin.y));
		ImVec2 tail = ImVec2((float)(waveletQueue[index]->tip.x + origin.x), (float)(waveletQueue[index]->tip.y + origin.y));
		ImVec2 tip = ImVec2((float)((waveletQueue[index]->tip.x * factor) + origin.x + waveletQueue[index]->tip.x), (float)((waveletQueue[index]->tip.y * factor) + origin.y + waveletQueue[index]->tip.y));

		waveletQueue[index]->totX += waveletQueue[index]->tip.x * factor;
		waveletQueue[index]->totY += waveletQueue[index]->tip.y * factor;
		waveletQueue[index]->numCoords++;

		waveletQueue[index]->pog = ImVec2((float)(waveletQueue[index]->totX / (double)waveletQueue[index]->numCoords),
			(float)(waveletQueue[index]->totY / (double)waveletQueue[index]->numCoords));

		cog = ImVec2(waveletQueue[index]->pog.x, waveletQueue[index]->pog.y);
		if (drawCircles)
		{
			draw_list->AddCircle(tip, 2.0f, IM_COL32(20, 125, 225, 255), 0, 2.0f);
		}

		if (drawEdges)
			draw_list->AddLine(tail, tip, waveletQueue[index]->color, waveletQueue[index]->thikness);
	}

	if (this->range >= this->maxRange)
	{
		pauseDemodulator = true;
		return; // notin more to do here
	}

	// just use the real part of an imaginary number which happens to be the y axis here
	// so the x axis represents the imaginary part, weired why it is not the other way arround
	double sinX = 2.0f * cog.y / waveletQueue[index]->radius;
	double cosX = 2.0f * cog.x / waveletQueue[index]->radius;
	double magnitude = sqrt((4 * cog.x * cog.x) + (4 * cog.y * cog.y)) / waveletQueue[index]->radius;

	output[0].AddPoint(this->range, (float)cosX);
	output[1].AddPoint((float)this->range, (float)sinX);
	output[2].AddPoint((float)this->range, (float)magnitude);

	double range = (double)(((int)(this->range * 100.0f) / 10)) / 10.0f;
	double amplitude = (double)((int)((sinX * 1000.0f) / 10) / 100.0f);
	if ((!isPositive && cosX >= 0.0f) || (isPositive && cosX < 0.0f))
	{
		if (range > 0.33f && abs(amplitude) >= 0.01f) {
			log.AddLog("pos:[%.4f] - cosx:[%.4f] - amplitude:[%.4f] - mag:[%.4f]\n", range, cosX, amplitude, magnitude);
			result.AddPoint((float)range, (float)amplitude);
		}
		isPositive = !isPositive;
	}

	this->range += stepFrequency;
}

void WaveletGenerator::DrawWavelet(ImDrawList* draw_list, int index, double t, double factor, ImVec2 origin, bool drawCircles, bool drawEdges)
{
	Rotate(index, t);
	ImVec2 center = ImVec2((float)(waveletQueue[index]->tail.x + origin.x), (float)(waveletQueue[index]->tail.y + origin.y));
	ImVec2 tail = ImVec2((float)(waveletQueue[index]->tip.x + origin.x), (float)(waveletQueue[index]->tip.y + origin.y));
	ImVec2 tip = ImVec2((float)((waveletQueue[index]->tip.x * factor) + origin.x + waveletQueue[index]->tip.x), (float)((waveletQueue[index]->tip.y * factor) + origin.y + waveletQueue[index]->tip.y));


	waveletQueue[index]->totX += waveletQueue[index]->tip.x * factor;
	waveletQueue[index]->totY += waveletQueue[index]->tip.y * factor;
	waveletQueue[index]->numCoords++;

	waveletQueue[index]->pog = ImVec2((float)(waveletQueue[index]->totX / (float)waveletQueue[index]->numCoords),
		(float)(waveletQueue[index]->totY / (double)waveletQueue[index]->numCoords));

	waveletQueue[index]->tip = ImVec2((float)(waveletQueue[index]->tip.x * factor + waveletQueue[index]->tip.x), (float)(waveletQueue[index]->tip.y * factor + waveletQueue[index]->tip.y));

	if (drawCircles)
	{
		draw_list->AddCircle(center, (float)abs(waveletQueue[index]->radius), waveletQueue[index]->color, 0, waveletQueue[index]->thikness);
		draw_list->AddCircle(tail, (float)abs(waveletQueue[index]->radius), waveletQueue[index]->color, 0, waveletQueue[index]->thikness);
	}

	if (drawEdges)
		draw_list->AddLine(tail, tip, waveletQueue[index]->color, waveletQueue[index]->thikness);
}

void WaveletGenerator::DrawWavelet(ImDrawList* draw_list, int index, double t, ImVec2 origin, bool drawCircles, bool drawEdges)
{
	Rotate(index, t);
	ImVec2 tail = ImVec2(waveletQueue[index]->tail.x + origin.x, waveletQueue[index]->tail.y + origin.y);
	ImVec2 tip = ImVec2(waveletQueue[index]->tip.x + origin.x, waveletQueue[index]->tip.y + origin.y);
	if (drawCircles)
		draw_list->AddCircle(tail, (float)abs(waveletQueue[index]->radius), waveletQueue[index]->color, 0, waveletQueue[index]->thikness);
	if (drawEdges)
		draw_list->AddLine(tail, tip, waveletQueue[index]->color, waveletQueue[index]->thikness);
}

void WaveletGenerator::DrawWavelets(ImDrawList* draw_list, double t, ImVec2 origin, bool drawCircles, bool drawEdges)
{
	for (int i = 0; i < waveletQueue.size(); i++)
	{
		DrawWavelet(draw_list, i, t, origin, drawCircles, drawEdges);
	}
}

void WaveletGenerator::DrawTraceLine(ImDrawList* draw_list, ImVec2 origin, bool drawEdges, double length, ImU32 color, float thickness)
{
	finalTip = waveletQueue.size() > 0 ? waveletQueue[waveletQueue.size() - 1]->tip : ImVec2(0.0f, 0.0f);
	if (!drawEdges) return;
	ImVec2 ftip = ImVec2(finalTip.x + origin.x, finalTip.y + origin.y);
	draw_list->AddLine(ftip, ImVec2((float)(ftip.x + length), (float)(ftip.y)), color, thickness);
}

ImVec2 WaveletGenerator::GetFinalTip()
{
	return finalTip;
}

int WaveletGenerator::GetSize()
{
	return (int)waveletQueue.size();
}

float WaveletGenerator::GetNormalizer()
{
	return normalizer;
}

void WaveletGenerator::SetRadius(double radius)
{
	if (radius > 2.0f)
	{
		this->radius = radius;
	}
}



void WaveletGenerator::AddWavelet(double frequency, double magnitude, ImU32 color, float thickness)
{
	Wavelet* wavelet = new Wavelet();
	wavelet->color = color;
	wavelet->thikness = thickness;
	wavelet->min = ImVec2(numeric_limits<float>::max(), numeric_limits<float>::max());
	wavelet->max = ImVec2(0.0f, 0.0f);
	wavelet->pog = ImVec2(0.0f, 0.0f);
	wavelet->numCoords = 0;
	wavelet->totX = 0;
	wavelet->totY = 0;

	wavelet->index = frequency * (4 / PI);
	wavelet->radius = magnitude;
	normalizer += abs(wavelet->radius);

	//TODO: maybe also specify if cos or sin is required here

	waveletQueue.push_back(wavelet);
}


void WaveletGenerator::AddWavelet(int index, ImU32 color, float thickness)
{
	Wavelet* wavelet = new Wavelet();
	wavelet->index = index;
	wavelet->color = color;
	wavelet->thikness = thickness;
	wavelet->min = ImVec2(numeric_limits<float>::max(), numeric_limits<float>::max());
	wavelet->max = ImVec2(0.0f, 0.0f);
	wavelet->pog = ImVec2(0.0f, 0.0f);
	wavelet->numCoords = 0;
	wavelet->totX = 0;
	wavelet->totY = 0;

	if (isinf(index * PI))
		wavelet->radius = 1.0f;
	else if (!this->useAlternateSeries)
	{
		wavelet->radius = (double)this->radius * (4 / (index * PI)); // square wave
	}
	else
	{
		wavelet->radius = (double)this->radius * (8 / (index * index * PI * PI)) * (index % 4 == 1 ? 1.0f : -1.0f); // triangle wave
	}

	if (isinf(wavelet->radius))
		wavelet->radius = 1.5f;

	normalizer += abs(wavelet->radius);

	waveletQueue.push_back(wavelet);
}

void WaveletGenerator::EnableAlternateSeries(bool enabled)
{
	useAlternateSeries = enabled;
}

ImVec2 WaveletGenerator::GetPog()
{
	if (waveletQueue.size() > 0)
		return waveletQueue[0]->pog;
	else
		return ImVec2(0.0f, 0.0f);
}

double WaveletGenerator::GetFrequency()
{
	return range;
}

bool WaveletGenerator::Pause()
{
	return pauseDemodulator;
}