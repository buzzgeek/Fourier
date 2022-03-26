#include "stb_image.h"
#include <iostream>
#include <complex>
#include <limits>
#include <math.h>
#include "fourier.h"
#include "imgui.h"
#include "implot.h"
#include "algorithm"

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
#define NUM_CYCLES 2.0f
#define GOLDEN_RATIO 1.6180339887 // appearently need to define a square

const char* fourier::concepts[] = { "fourier series",
									"fourier transform (sinusoidal)",
									"demodulate",
									"dft 2 epicycles",
									"dft 1 epicycle",
									"capture path", };

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
										"custom",
										"square", };
//"inv. custom",}; broken

const char* fourier::curves[] = { "sin(x)",
								"cos(x)",
								"sin(x)^2 + cos(x)",
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
								"dataAnalog[2]", };

ExampleAppConsole fourier::console = {};
ExampleAppLog fourier::log = {};
WaveletGenerator fourier::waveletGenerator = { 64 };
ScrollingBuffer fourier::tracer = { 100000 };
ScrollingBuffer fourier::result = { 1000000 };
ScrollingBuffer fourier::dataAnalog[3] = { {MAX_PLOT}, {MAX_PLOT}, };
ScrollingBuffer fourier::dataModulated = { MAX_PLOT };
ScrollingBuffer fourier::demodulator[NUM_DEMODULATOR_GRAPHS] = { {MAX_PLOT},
	{MAX_PLOT},
	{MAX_PLOT},
	{MAX_PLOT},
	{MAX_PLOT},
	{MAX_PLOT}, };

std::vector<float> fourier::Xaxis = {};
std::vector<float> fourier::Yaxis = {};
std::vector<WaveletStruct> fourier::Xdft = {};
std::vector<WaveletStruct> fourier::Ydft = {};
std::vector<WaveletStruct> fourier::Cdft = {};

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
	case 3:
		// TODO draw plots?
		break;
	case 4:
		// draw image to capture paths
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

#pragma region init_canvas
	static ImVector<ImVec2> points;
	static ImVec2 scrolling(0.0f, 0.0f);
	static bool opt_enable_grid = true;
	static bool opt_enable_context_menu = true;
	static bool opt_enable_image = false;
	static bool adding_line = false;
	static bool stop_capture = false;

	ImGui::Checkbox("Enable grid", &opt_enable_grid); ImGui::SameLine();
	ImGui::Checkbox("Enable context menu", &opt_enable_context_menu); ImGui::SameLine();
	if(concept_current != 5)
		ImGui::Checkbox("Enable image", &opt_enable_image);
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


	ImVec2 image_pos = ImVec2(canvas_p0.x + (canvas_sz.x / 2) + scrolling.x, canvas_p0.y + (canvas_sz.y / 2) + scrolling.y);

	if (opt_enable_image || concept_current == 5)
		DrawBackground(draw_list, image_pos);

	// Add first and second point
	if (is_hovered && !stop_capture && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		points.push_back(mouse_pos_in_canvas);
		// need to add temporary point that will be replaced in the next if statement by the most current mouse cursor's position
		points.push_back(mouse_pos_in_canvas);
		result.AddPoint(mouse_pos_in_canvas.x, mouse_pos_in_canvas.y);
	}

	if (points.size() > 0 && !stop_capture)
	{
		points.push_back(mouse_pos_in_canvas);
		result.AddPoint(mouse_pos_in_canvas.x, mouse_pos_in_canvas.y);
	}

	if (is_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		stop_capture = true;
	}

	// Pan (we use a zero mouse threshold when there's no context menu)
	// You may decide to make that threshold dynamic based on whether the mouse is hovering something etc.
	const float mouse_threshold_for_pan = opt_enable_context_menu ? -1.0f : 0.0f;
	if (is_active && ImGui::IsMouseDragging(ImGuiMouseButton_Right, mouse_threshold_for_pan))
	{
		scrolling.x += io.MouseDelta.x;
		scrolling.y += io.MouseDelta.y;
		tracer.Erase();
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
		//if (ImGui::MenuItem("Remove one", NULL, false, points.Size > 0)) { points.resize(points.size() - 2); }
		if (ImGui::MenuItem("Remove all", NULL, false, points.Size > 0)) 
		{ 
			points.clear(); 
			stop_capture = false;
			result.Erase();
		}
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
	for (int n = 0; n < points.Size; n += 1)
		if(n + 1 < points.Size)
			draw_list->AddLine(ImVec2(origin.x + points[n].x, origin.y + points[n].y), ImVec2(origin.x + points[n + 1].x, origin.y + points[n + 1].y), IM_COL32(255, 255, 0, 255), 2.0f);
#pragma endregion init_canvas

	if (concept_current == 5) // we are done here, capture path of image only
	{
		draw_list->PopClipRect();
		ImGui::End();// end canvas
		return;
	}

#pragma region draw_wavelets

	ImVec2 circle_pos = ImVec2(canvas_p0.x + (canvas_sz.x / 2) + scrolling.x, canvas_p0.y + (canvas_sz.y / 2) + scrolling.y);
	ImVec2 tip, e1, e2, ec;

	switch (concept_current) {
	case 0: // fourier series
		waveletGenerator.DrawWavelets(draw_list, time, circle_pos, showCircles, showEdges);
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
	case 3: //dft 2 epicycles
		e2 = DrawEpiCycles(origin.x, origin.y, 0.0f, Xdft, time);
		e1 = DrawEpiCycles(origin.x , origin.y, PI / 2.0f, Ydft, time);

		if (showEdges)
		{
			draw_list->AddLine(ImVec2(e1.x, e1.y), ImVec2(e2.x, e1.y), IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
			draw_list->AddLine(ImVec2(e2.x, e2.y), ImVec2(e2.x, e1.y), IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		}
		tracer.AddPoint(e2.x - circle_pos.x, e1.y - circle_pos.y);
		break;
	case 4: //dft 1 epicycle
//		ec = DrawEpiCycles(circle_pos.x, circle_pos.y, 0.0f, Cdft, time);
		ec = DrawEpiCycles(origin.x, origin.y, 0.0f, Cdft, time);
		tracer.AddPoint(ec.x - circle_pos.x, ec.y - circle_pos.y);
		break;
	}

	if (concept_current >= 3)
		time += static_cast<float>(TWO_PI / Cdft.size());
	else
		time += static_cast<float>(TWO_PI / timeChangeRate);

	if (time >= TWO_PI)
		time = 0.0f;

	if (concept_current != 2)
	{
		if (concept_current < 3)
		{
			if (tracer.Data.Size > 0)
				tracer.AddPoint(waveletGenerator.GetFinalTip().x, waveletGenerator.GetFinalTip().y);
			else
				tracer.AddPoint(0.0f, 0.0f);
		}
		ImVec2 p = { 0.0f, 0.0f };


		for (int n = 1; n < tracer.Data.Size; n += 1)
			if (n + 1 < tracer.Data.Size)
				draw_list->AddLine(ImVec2(circle_pos.x + tracer.Data[n].x, circle_pos.y + tracer.Data[n].y), 
					ImVec2(circle_pos.x + tracer.Data[n + 1].x, circle_pos.y + tracer.Data[n + 1].y), 
					IM_COL32(20, 125, 225, 255), 2.0f);

		for (int i = 0; i < tracer.Data.size(); i++)
		{
			p = ImVec2(tracer.Data[i].x + circle_pos.x, tracer.Data[i].y + circle_pos.y);
			if (i == 0)
			{
				draw_list->AddCircle(p, 5.0f, IM_COL32(255, 20, 125, 255), 0, 2.0f);
			}
			else
			{
				draw_list->AddCircle(p, 1.0f, IM_COL32(20, 125, 225, 255), 0, 1.0f);
			}
		}

		draw_list->AddCircle(p, 3.0f, IM_COL32(255, 20, 125, 255), 0, 2.0f);
	}

	draw_list->AddCircle(circle_pos, 3.0f, IM_COL32(255, 20, 125, 255), 0, 2.0f);
	ImVec2 pog = ImVec2(waveletGenerator.GetPog().x + circle_pos.x,
		waveletGenerator.GetPog().y + circle_pos.y);
	draw_list->AddCircle(pog, 10.0f, IM_COL32(200, 200, 80, 255), 0, 2.0f);

#pragma endregion draw_wavelets

	draw_list->PopClipRect();
	ImGui::End();// end canvas

}

void fourier::DrawProperties()
{
	static float f = 0.0f;
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
	if (concept_current > 0) {
		changed_curve = ImGui::ListBox("Curve", &curve_current, curves, IM_ARRAYSIZE(curves), 3);
	}
	updateRequired = changed_curve || updateRequired;
	ImGui::Separator();

	//if (strategy_current < 4 && concept_current != 1) // primes have set number of nodes, ie slider does not do anything for primes series
	updateRequired = ImGui::SliderInt("Num of Nodes", &numNodes, 1, MAX_NODES) || updateRequired;

	//if (concept_current != 2) // primes have set number of nodes, ie slider does not do anything for primes series
	updateRequired = ImGui::SliderFloat("Slowmo Rate Canvas", &timeChangeRate, 10.0f, 10000.0f) || updateRequired;
	updateRequired = ImGui::SliderFloat("Slowmo Rate Plot", &plotTimeChangeRate, 10.0f, 10000.0f) || updateRequired;
	updateRequired = ImGui::SliderFloat("Radius", &radiusCircle, 2.0f, 512.0f /*65536.0f*/) || updateRequired;
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
		for (int i = 0; i < NUM_DEMODULATOR_GRAPHS; i++)
		{
			demodulator[i].Erase();
		}
		timePlot = 0.0f;
		waveletGenerator.SetRadius(radiusCircle);
		time = 0.0f;
		Setup();

		if (concept_current != 2)
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


void fourier::DrawBackground(ImDrawList* draw_list, ImVec2 offset)
{
	ImGuiIO& io = ImGui::GetIO();
	MyData* myData = (MyData*)io.UserData;
	ImTextureID my_tex_id = myData->textureId;
	float my_tex_w = myData->width;
	float my_tex_h = myData->height;
	ImVec2 uv_min = ImVec2(0.0f, 0.0f);                 // Top-left
	ImVec2 uv_max = ImVec2(1.0f, 1.0f);                 // Lower-right
	draw_list->AddImage(my_tex_id, ImVec2(offset.x - (my_tex_w/2.0f), offset.y - (my_tex_h/2.0f)), ImVec2(offset.x + (my_tex_w/2.0f), offset.y + (my_tex_h/2.0f)), uv_min, uv_max);
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
	static bool showAnalog[NUM_DEMODULATOR_GRAPHS] = { true, true, true, true, false, false };
	float x = 0.0f;
	char label[32];
	ImGui::Checkbox("cos(x)", &showAnalog[0]); ImGui::SameLine();
	ImGui::Checkbox("sin(x)", &showAnalog[1]); ImGui::SameLine();
	ImGui::Checkbox("magnitude", &showAnalog[2]); ImGui::SameLine();
	ImGui::Checkbox("-magnitude", &showAnalog[3]); ImGui::SameLine();
	ImGui::Checkbox("sin(x)+cos(x)", &showAnalog[4]); ImGui::SameLine();
	ImGui::Checkbox("sin(x)-cos(x)", &showAnalog[5]);

	float range = TWO_PI;
	float minY = 100.0f;
	float maxY = 0.0f;

	finalY = 0.0f;
	finalX = 0.0f;
	float tmp = 0.0f;
	float rDiv2 = 1.0f;

	for (int i = 0; i < dataModulated.MaxSize; i++) {
		switch (curve_current) {
		case 0: // cos(x)
			finalY = sin(x);
			break;
		case 1: // cos(x)
			finalY = cos(x);
			break;
		case 2: //sin(x)^2 + cos(x)
			finalY = (((sin(x) * sin(x)) + cos(x)));
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
				finalY += static_cast<float>((4 * sin(x * tmp)) / (PI * tmp));
			}

			break;
		case 11: // sum(sin(2x - 1)) : odd integers / square wave
			finalY = 0.0f;
			for (int i = 1; i < (numNodes + 1); i++)
			{
				tmp = (i * 2.0f) - 1.0f;
				finalY += static_cast<float>((4 * sin(x * tmp)) / (PI * tmp));
			}
			break;
		case 12: // square
			rDiv2 = 50.0f;

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

			finalY -= rDiv2;

			assert(finalY < (2 * rDiv2));
			break;
		case 13://"sin(x)  - sin(2x) + sin(3x) - sin(4x) + sin(5x)....",
			finalY = 0.0f;
			for (int i = 1; i < (numNodes + 1); i++)
			{
				tmp = static_cast<float>(i);
				finalY += i % 2 ? sin(x * tmp) : -sin(x * tmp);
			}
			break;
		case 14://	"-sin(x)  + sin(2x) - sin(3x) + sin(4x) - sin(5x)....",
			finalY = 0.0f;
			for (int i = 1; i < (numNodes + 1); i++)
			{
				tmp = static_cast<float>(i);
				finalY += !(i % 2) ? sin(x * tmp) : -sin(x * tmp);
			}
			break;
		case 15://"dataAnalog[2]"
			// TODO: use a more dedicated buffer instead of the index 2 -> it is not very obvious right now, why this one can be used
			if (dataAnalog[2].Data.size() > i)
				finalY = dataAnalog[2].Data[i].y;
			break;
		}

		minY = IM_MIN(minY, finalY);
		maxY = IM_MAX(maxY, finalY);

		x += range / dataModulated.MaxSize;

		dataModulated.AddPoint(x, finalY);
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
				ImPlot::PlotLine(label, &demodulator[2].Data[0].x, &demodulator[2].Data[0].y, demodulator[2].Data.size(), demodulator[2].Offset, 2 * sizeof(float));
		}
		if (showAnalog[3])
		{
			strcpy_s(label, 32, "-magnitude");
			if (demodulator[3].Data.size() > 0)
				ImPlot::PlotLine(label, &demodulator[3].Data[0].x, &demodulator[3].Data[0].y, demodulator[3].Data.size(), demodulator[3].Offset, 2 * sizeof(float));
		}
		if (showAnalog[4])
		{
			strcpy_s(label, 32, "sin(x)+cos(x)");
			if (demodulator[4].Data.size() > 0)
				ImPlot::PlotLine(label, &demodulator[4].Data[0].x, &demodulator[4].Data[0].y, demodulator[4].Data.size(), demodulator[4].Offset, 2 * sizeof(float));
		}
		if (showAnalog[5])
		{
			strcpy_s(label, 32, "sin(x)-cos(x)");
			if (demodulator[5].Data.size() > 0)
				ImPlot::PlotLine(label, &demodulator[5].Data[0].x, &demodulator[5].Data[0].y, demodulator[5].Data.size(), demodulator[5].Offset, 2 * sizeof(float));
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
	static float prevX = 1.0f;

	char label[32];
	ImGui::Checkbox("cos(x)", &showAnalog[0]);  ImGui::SameLine();
	ImGui::Checkbox("sin(x)", &showAnalog[1]);

	switch (curve_current) {
	case 0: // cos(x)
		finalY = sin(timePlot);
		finalX = cos(timePlot);
		break;
	case 1: // cos(x)
		finalY = cos(timePlot);
		finalX = -sin(timePlot);
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

	if (!paused) {
		timePlot += static_cast<float>(PI / plotTimeChangeRate); //ImGui::GetIO().DeltaTime;
		if (showAnalog[0])
			dataAnalog[0].AddPoint(-timePlot, finalX);
		if (showAnalog[1])
			dataAnalog[1].AddPoint(-timePlot, finalY);
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
	ImGui::Checkbox("re", &showAnalog[0]);  ImGui::SameLine();
	ImGui::Checkbox("im", &showAnalog[1]);

	if (!paused) {
		timePlot += static_cast<float>(PI / plotTimeChangeRate); //ImGui::GetIO().DeltaTime;
		if (showAnalog[0])
			dataAnalog[0].AddPoint(-timePlot, finalX);
		if (showAnalog[1])
			dataAnalog[1].AddPoint(-timePlot, finalY);
	}

	if ((concept_current != 2) && !(strategy_current == 10 && concept_current == 0))
	{
		dataAnalog[2].AddPoint(-timePlot, finalX);
		log.AddLog("adding %.3f:%.3f to cache\n", timePlot, finalX);
	}

	if (ImPlot::BeginPlot("##Digital", ImGui::GetContentRegionAvail())) {
		ImPlot::SetupAxisLimits(ImAxis_X1, -timePlot + 10.0, -timePlot, paused ? ImGuiCond_Once : ImGuiCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, -1.875, 1.875);
		for (int i = 0; i < 2; ++i) {
			if (showAnalog[i]) {
				strcpy_s(label, 32, i ? "im" : "re");
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
				waveletGenerator.AddWavelet(true, (result.Data[i].x * radiusCircle), (result.Data[i].y * radiusCircle));
			}
		}
		break;
	case 11: // Square
		//radiusCircle = radiusCircle * static_cast<float>(4.0f / PI);
		//waveletGenerator.AddWavelet(false, 0.0f, 0.122f * radiusCircle);
		//waveletGenerator.AddWavelet(false, 4.0f, -0.155f * radiusCircle);
		//waveletGenerator.AddWavelet(false, 8.0f, .05f * radiusCircle);
		//waveletGenerator.AddWavelet(false, 12.0f, -0.023f * radiusCircle);
		//waveletGenerator.AddWavelet(false, 16.0f, .014f * radiusCircle);
		//waveletGenerator.AddWavelet(false, 20.0f, -0.009f * radiusCircle);
		//waveletGenerator.AddWavelet(false, 24.0f, +0.006f * radiusCircle);

		waveletGenerator.AddWavelet(false, 0.0f, 6.109f * radiusCircle);
		waveletGenerator.AddWavelet(false, 4.0f, -7.823f * radiusCircle);
		waveletGenerator.AddWavelet(false, 8.0f, 2.468f * radiusCircle);
		waveletGenerator.AddWavelet(false, 12.0f, -1.171f * radiusCircle);
		waveletGenerator.AddWavelet(false, 16.0f, .676f * radiusCircle);
		waveletGenerator.AddWavelet(false, 20.0f, -0.439f * radiusCircle);
		waveletGenerator.AddWavelet(false, 24.0f, 0.307f * radiusCircle);
		waveletGenerator.AddWavelet(false, 28.0f, -0.227f * radiusCircle);
		waveletGenerator.AddWavelet(false, 32.0f, 0.174f * radiusCircle);
		waveletGenerator.AddWavelet(false, 36.0f, -0.138f * radiusCircle);
		waveletGenerator.AddWavelet(false, 40.0f, 0.112f * radiusCircle);

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

	// testing
	std::vector<float> xdata = {};
	std::vector<float> ydata = {};

	std::vector<Complex> data = {};


	if (result.Data.size() > 0)
	{
		for (int i = 1; i < result.Data.size(); i++)
		{
			xdata.push_back(result.Data[i].x);
			ydata.push_back(result.Data[i].y);
			data.push_back(Complex(result.Data[i].x, result.Data[i].y));
		}
	}
	else
	{
		// a square ???
		for (int i = 0; i <= 100; i++)
		{
			xdata.push_back(static_cast<float>(i));
			ydata.push_back(100.0f);
			data.push_back(Complex(static_cast<float>(i), 100.0f));
		}
		for (int i = 100; i >= 0; i--)
		{
			xdata.push_back(100.0f);
			ydata.push_back(static_cast<float>(i));
			data.push_back(Complex(100.0f, static_cast<float>(i)));
		}
		for (int i = 100; i >= 0; i--)
		{
			xdata.push_back(static_cast<float>(i));
			ydata.push_back(0);
			data.push_back(Complex(static_cast<float>(i), 0.0f));
		}
		for (int i = 0; i <= 100; i++)
		{
			xdata.push_back(0.0f);
			ydata.push_back(static_cast<float>(i));
			data.push_back(Complex(0.0f, static_cast<float>(i)));
		}
	}

	//for (int i = 0; i < 100; i++)
	//{
	//	float angle = i * (TWO_PI / 100.0f);
	//	float x = 10 * cos(angle);
	//	float y = 10 * sin(angle);
	//	xdata.push_back(x);
	//	ydata.push_back(y);
	//}

	// get the discrete fourier components
	int s = static_cast<int>(xdata.size());
	//int s = numNodes;

	Xdft = DFT(xdata, s);
	Ydft = DFT(ydata, s);
	Cdft = DFT(data, s);


	std::sort(Xdft.begin(), Xdft.end(), greater_than_key());
	std::sort(Ydft.begin(), Ydft.end(), greater_than_key());
	std::sort(Cdft.begin(), Cdft.end(), greater_than_key());
}

WaveletGenerator::WaveletGenerator(float radius)
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

void WaveletGenerator::Rotate(bool isClockwise, int i, float t)

{
	//float pi = acosf(-1);
	//float re = 0.0f;
	//float im = 0.0f;

	//_Fcomplex dt = { 0.0F, t * waveletQueue[i]->index };
	//_Fcomplex rot = cexpf(dt);
	//float phase = atan2(cimagf(dt), crealf(dt)); //?

	//float img = cimag(I);
	//float e1 = expf(img * pi);
	//float e2 = expf(img * 0);
	//_Fcomplex ce1 = cexpf({ 0.0f, 0.0f });
	//_Fcomplex ce2 = cexpf({ 0.0f, pi });
	//_Fcomplex ce3 = cexpf({ 0.0f, pi / 2.0f });
	//_Fcomplex ce4 = cexpf({ 0.0f, (3.0f * pi) / 2.0f });
	//_Fcomplex ce5 = cexpf({ 0.0f, 1.234567f });

	//float check1 = sqrt(crealf(ce1) * crealf(ce1) + cimagf(ce1) * cimagf(ce1));// -1.0f;
	//float check2 = sqrt(crealf(ce2) * crealf(ce2) + cimagf(ce2) * cimagf(ce2));// -1.0f;
	//float check3 = sqrt(crealf(ce3) * crealf(ce3) + cimagf(ce3) * cimagf(ce3));// -1.0f;
	//float check4 = sqrt(crealf(ce4) * crealf(ce4) + cimagf(ce4) * cimagf(ce4));// -1.0f;
	//float check5 = sqrt(crealf(ce5) * crealf(ce5) + cimagf(ce5) * cimagf(ce5));// -1.0f;

	waveletQueue[i]->rotation = ImVec2(waveletQueue[i]->radius * cos(t * waveletQueue[i]->index), waveletQueue[i]->radius * sin(t * waveletQueue[i]->index));

	if (!isClockwise)
	{
		waveletQueue[i]->rotation.y *= -1;
	}

	if (i > 0)
		waveletQueue[i]->tail = ImVec2(waveletQueue[i - 1]->tip.x, waveletQueue[i - 1]->tip.y);
	else
		waveletQueue[0]->tail = ImVec2(0.0f, 0.0f);

	waveletQueue[i]->tip = ImVec2(waveletQueue[i]->tail.x + waveletQueue[i]->rotation.x, waveletQueue[i]->tail.y + waveletQueue[i]->rotation.y);
}

// will draw a full data set wound arround the wavelet numOfTimes times
void WaveletGenerator::DrawWavelet(ExampleAppLog& log,
	ImDrawList* draw_list,
	float& plotTimeChangeRate,
	int index,
	ScrollingBuffer& curve,
	ScrollingBuffer* demodulator,
	ScrollingBuffer& result,
	int numOfTimes,
	ImVec2 origin,
	bool drawCircles,
	bool drawEdges)
{
	float factor = 1.0f;
	float stepFrequency = (numOfTimes * TWO_PI / plotTimeChangeRate);
	static bool isPositive = false;

	this->maxRange = numOfTimes + 1.0f;

	waveletQueue[index]->numCoords = 0;
	waveletQueue[index]->totX = 0.0f;
	waveletQueue[index]->totY = 0.0f;

	float rotation = 0.0f;
	float rotationStep = (TWO_PI * this->range) / curve.Data.size();
	ImVec2 cog;

	for (int i = 0; i < curve.Data.Size; i++)
	{
		factor = curve.Data[i].y; // here is a tricky problem
		rotation += rotationStep;
		Rotate(waveletQueue[index]->isClockwise, index, -rotation);

		ImVec2 center = ImVec2(waveletQueue[index]->tail.x + origin.x, waveletQueue[index]->tail.y + origin.y);
		ImVec2 tail = ImVec2(waveletQueue[index]->tip.x + origin.x, waveletQueue[index]->tip.y + origin.y);
		ImVec2 tip = ImVec2((waveletQueue[index]->tip.x * factor) + origin.x + waveletQueue[index]->tip.x, (waveletQueue[index]->tip.y * factor) + origin.y + waveletQueue[index]->tip.y);

		waveletQueue[index]->totX += waveletQueue[index]->tip.x * factor;
		waveletQueue[index]->totY += waveletQueue[index]->tip.y * factor;
		waveletQueue[index]->numCoords++;

		waveletQueue[index]->pog = ImVec2(waveletQueue[index]->totX / static_cast<float>(waveletQueue[index]->numCoords),
			waveletQueue[index]->totY / static_cast<float>(waveletQueue[index]->numCoords));

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
	float sinX = 2.0f * cog.y / waveletQueue[index]->radius;
	float cosX = 2.0f * cog.x / waveletQueue[index]->radius;
	float magnitude = sqrt((4 * cog.x * cog.x) + (4 * cog.y * cog.y)) / waveletQueue[index]->radius;

	demodulator[0].AddPoint(this->range, cosX);
	demodulator[1].AddPoint(this->range, -sinX);
	demodulator[2].AddPoint(this->range, magnitude);
	demodulator[3].AddPoint(this->range, -magnitude);
	demodulator[4].AddPoint(this->range, sinX + cosX);
	demodulator[5].AddPoint(this->range, sinX - cosX);

	float range = static_cast<float>((static_cast<int>(this->range * 100.0f) / 10)) / 10.0f;
	float amplitude = static_cast<float>(static_cast<int>((sinX * 1000.0f) / 10) / 100.0f);
	//if ((!isPositive && cosX >= 0.0f) || (isPositive && cosX < 0.0f))
	//{
	//	if (range > 0.33f && abs(amplitude) >= 0.01f) {
	//		log.AddLog("pos:[%.4f] - cosx:[%.4f] - amplitude:[%.4f] - mag:[%.4f]\n", range, cosX, amplitude, magnitude);
	//		result.AddPoint(range, amplitude);
	//	}
	//	isPositive = !isPositive;
	//}

	this->range += stepFrequency;
}

void WaveletGenerator::DrawWavelet(ImDrawList* draw_list, int index, float t, float factor, ImVec2 origin, bool drawCircles, bool drawEdges)
{
	Rotate(waveletQueue[index]->isClockwise, index, t);
	ImVec2 center = ImVec2(waveletQueue[index]->tail.x + origin.x, waveletQueue[index]->tail.y + origin.y);
	ImVec2 tail = ImVec2((waveletQueue[index]->tip.x + origin.x), (waveletQueue[index]->tip.y + origin.y));
	ImVec2 tip = ImVec2(((waveletQueue[index]->tip.x * factor) + origin.x + waveletQueue[index]->tip.x), ((waveletQueue[index]->tip.y * factor) + origin.y + waveletQueue[index]->tip.y));

	waveletQueue[index]->totX += waveletQueue[index]->tip.x * factor;
	waveletQueue[index]->totY += waveletQueue[index]->tip.y * factor;
	waveletQueue[index]->numCoords++;

	waveletQueue[index]->pog = ImVec2((waveletQueue[index]->totX / waveletQueue[index]->numCoords),
		(waveletQueue[index]->totY / static_cast<float>(waveletQueue[index]->numCoords)));

	waveletQueue[index]->tip = ImVec2((waveletQueue[index]->tip.x * factor + waveletQueue[index]->tip.x), (waveletQueue[index]->tip.y * factor + waveletQueue[index]->tip.y));

	if (drawCircles)
	{
		draw_list->AddCircle(center, abs(waveletQueue[index]->radius), waveletQueue[index]->color, 0, waveletQueue[index]->thikness);
		draw_list->AddCircle(tail, abs(waveletQueue[index]->radius), waveletQueue[index]->color, 0, waveletQueue[index]->thikness);
	}

	if (drawEdges)
		draw_list->AddLine(tail, tip, waveletQueue[index]->color, waveletQueue[index]->thikness);
}

void WaveletGenerator::DrawWavelet(ImDrawList* draw_list, int index, float t, ImVec2 origin, bool drawCircles, bool drawEdges)
{
	Rotate(waveletQueue[index]->isClockwise, index, t);

	ImVec2 tail = ImVec2(waveletQueue[index]->tail.x + origin.x, waveletQueue[index]->tail.y + origin.y);
	ImVec2 tip = ImVec2(waveletQueue[index]->tip.x + origin.x, waveletQueue[index]->tip.y + origin.y);
	if (drawCircles)
		draw_list->AddCircle(tail, abs(waveletQueue[index]->radius), waveletQueue[index]->color, 0, waveletQueue[index]->thikness);
	if (drawEdges)
		draw_list->AddLine(tail, tip, waveletQueue[index]->color, waveletQueue[index]->thikness);
}

void WaveletGenerator::DrawWavelets(ImDrawList* draw_list, float t, ImVec2 origin, bool drawCircles, bool drawEdges)
{
	for (int i = 0; i < waveletQueue.size(); i++)
	{
		DrawWavelet(draw_list, i, t, origin, drawCircles, drawEdges);
	}
}

void WaveletGenerator::DrawTraceLine(ImDrawList* draw_list, ImVec2 origin, bool drawEdges, float length, ImU32 color, float thickness)
{
	finalTip = waveletQueue.size() > 0 ? waveletQueue[waveletQueue.size() - 1]->tip : ImVec2(0.0f, 0.0f);
	if (!drawEdges) return;
	ImVec2 ftip = ImVec2(finalTip.x + origin.x, finalTip.y + origin.y);
	draw_list->AddLine(ftip, ImVec2((ftip.x + length), (ftip.y)), color, thickness);
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

void WaveletGenerator::SetRadius(float radius)
{
	if (radius > 2.0f)
	{
		this->radius = radius;
	}
}

void WaveletGenerator::AddWavelet(bool isClockwise, float frequency, float magnitude, ImU32 color, float thickness)
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

	wavelet->index = frequency;
	wavelet->radius = magnitude;
	wavelet->isClockwise = isClockwise;
	normalizer += abs(wavelet->radius);

	waveletQueue.push_back(wavelet);
}


void WaveletGenerator::AddWavelet(int index, ImU32 color, float thickness)
{
	Wavelet* wavelet = new Wavelet();
	wavelet->index = static_cast<float>(index);
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
		wavelet->radius = static_cast<float>(this->radius * (4 / (index * PI))); // square wave
	}
	else
	{
		wavelet->radius = static_cast<float>(this->radius * (8 / (index * index * PI * PI)) * (index % 4 == 1 ? 1.0f : -1.0f)); // triangle wave
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

float WaveletGenerator::GetFrequency()
{
	return range;
}

bool WaveletGenerator::Pause()
{
	return pauseDemodulator;
}

// discrete fourier transform converts a set of float values to a set of strucklets
// the float values either contain all x axis or all y axis values of a given path to be drawn
// eg. in this case the dft needs to be performed twice, once for the x axis values and once for the y axis values
std::vector<WaveletStruct> fourier::DFT(const std::vector<float> curve, int max_freq)
{
	std::vector<WaveletStruct> res;
	const size_t N = curve.size();

	// k represents each discrete frequency
	for (int k = 0; k < max_freq; k++)
	{
		WaveletStruct wavelet;
		wavelet.re = 0.0f;
		wavelet.im = 0.0f;
		for (int n = 0; n < N; n++)
		{
			float phi = (TWO_PI * k * n) / N;
			wavelet.re += curve[n] * cos(phi);
			wavelet.im -= curve[n] * sin(phi);
		}

		wavelet.re = wavelet.re / N;
		wavelet.im = wavelet.im / N;

		wavelet.frequency = static_cast<float>(k);
		wavelet.amplitude = sqrt(wavelet.re * wavelet.re + wavelet.im * wavelet.im);

		wavelet.phase = atan2(wavelet.im, wavelet.re);

		res.push_back(wavelet);
	}

	return res;
}

std::vector<WaveletStruct> fourier::DFT(const std::vector<Complex> curve, int max_freq)
{
	std::vector<WaveletStruct> res;
	const size_t N = curve.size();

	for (int k = 0; k < max_freq; k++)
	{
		Complex sum(0.0f, 0.0f);

		for (int n = 0; n < N; n++)
		{
			const float phi = (TWO_PI * k * n) / N;
			const Complex c(cos(phi), -sin(phi));
			Complex tmp = curve[n];
			sum.add(tmp.mult(c));
		}

		sum.re = sum.re / N;
		sum.im = sum.im / N;

		WaveletStruct wavelet;
		wavelet.re = sum.re;
		wavelet.im = sum.im;
		wavelet.frequency = static_cast<float>(k);
		wavelet.amplitude = sqrt(wavelet.re * wavelet.re + wavelet.im * wavelet.im);

		wavelet.phase = atan2(wavelet.im, wavelet.re);

//		if(abs(wavelet.amplitude)>0.001)
		res.push_back(wavelet);
	}

	return res;
}

ImVec2 fourier::DrawEpiCycles(float origin_x, float origin_y, float rotation, std::vector<WaveletStruct>& fourier, float time)
{
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	float x = origin_x;
	float y = origin_y;

	radiusCircle = 1.0f;

	for (int i = 0; i < fourier.size(); i++)
	{
		float prevx = x;
		float prevy = y;

		x += radiusCircle * fourier[i].amplitude * cos(fourier[i].frequency * time + fourier[i].phase + rotation);
		y += radiusCircle * fourier[i].amplitude * sin(fourier[i].frequency * time + fourier[i].phase + rotation);

		if (showCircles)
			draw_list->AddCircle(ImVec2(prevx, prevy), fourier[i].amplitude * radiusCircle, IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
		if (showEdges)
			draw_list->AddLine(ImVec2(prevx, prevy), ImVec2(x, y), IM_COL32(circle_color.x * 255, circle_color.y * 255, circle_color.z * 255, 255));
	}

	return ImVec2(x, y);
}