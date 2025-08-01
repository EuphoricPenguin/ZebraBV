/**
 * OpenBoardView
 *
 * Copyright inflex 2016 (Paul Daniels)
 * Copyright chloridite 2016
 *
 * https://github.com/OpenBoardView/OpenBoardView
 *
 */

#include "platform.h" // Should be kept first
#include "utils.h"
#include "version.h"

#include "BoardView.h"
#include "history.h"

#include "FileFormats/FZFile.h"
#include "confparse.h"
#include "resource.h"
#include <cmath>
#include <SDL.h>
#include <chrono>
#include <memory>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#ifndef _MSC_VER
#include <unistd.h>
#endif

// Rendering stuff
#include "Renderers/Renderers.h"
#include "GUI/DPI.h"
#include "GUI/Fonts.h"

#include "filesystem_impl.h"

// Handling of DDE command line argument for PDFBridge
#ifdef _WIN32
#include "PDFBridge/PDFBridgeSumatra.h"
#endif

struct globals {
	char *input_file = nullptr;
	char *config_file = nullptr;
	bool slowCPU = false;
	int width = 0;
	int height = 0;
	int dpi = 0;
	float font_size = 0.0f;
	bool debug = false;
	Renderers::Renderer renderer = Renderers::Renderer::DEFAULT;
#ifdef _WIN32
	char *pdfBridgePdfPath = nullptr;
	char *pdfBridgeSearchStr = nullptr;
#endif
};

static SDL_Window *window      = nullptr;

char help[] =
    " [-h] [-V] [-l] [-c <config file>] [-i <intput file>] [-x <width>] [-y <height>] [-z <fontsize>] [-p <dpi>] [-r <renderer>] [-d]\n\
	-h : This help\n\
	-V : Version information\n\
	-l : slow CPU mode, disables AA and other items to try provide more FPS\n\
	-c <config file> : alternative configuration file (default is ~/.config/" OBV_NAME
    "/obv.conf)\n\
	-i <input file> : board file to load\n\
	-x <width> : Set window width\n\
	-y <height> : Set window height\n\
	-z <pixels> : Set font size\n\
	-p <dpi> : Set the dpi\n\
	-r <renderer> : Set the renderer [ OPENGL1 = 1; OPENGL3 = 2; OPENGLES2 = 3 ]\n\
	-d : Debug mode\n\
";

int parse_parameters(int argc, char **argv, struct globals *g) {
	int param;

	/**
	 * Decode the input parameters.
	 * Yes, I know, I should do this using gnu params etc.
	 */
	for (param = 1; param < argc; param++) {
		char *p = argv[param];

		// macOS quirk: Ignore psn_* parameter passed by launchd
		if (strncmp(p, "-psn_", 5) == 0) {
			continue;
		}

		if (strcmp(p, "-h") == 0) {
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s %s", argv[0], help);
			exit(0);
		}

		if (strcmp(p, "-V") == 0) {
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "OFBV-BUILD: %s %s\n", OBV_BUILD, __DATE__ " " __TIME__);
			exit(0);
		}

		if (strcmp(p, "-c") == 0) {
			param++;
			if ((param < argc)&&(argv[param][0] != '-')) {
				g->config_file = argv[param];
			} else {
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Not enough paramters for -c <config>\n\n%s %s", argv[0], help );
				exit(1);
			}

		} else if (strcmp(p, "-i") == 0) {
			param++;
			if ((param < argc)&&(argv[param][0] != '-')) {
				g->input_file = argv[param];
			} else {
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Not enough paramters for -i <input file>\n\n%s %s", argv[0], help );
				exit(1);
			}

		} else if (strcmp(p, "-x") == 0) {
			param++;
			if ((param < argc)&&(argv[param][0] != '-')) {
				g->width = strtol(argv[param], NULL, 10);
			} else {
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Not enough paramters for -x <window width>\n\n%s %s", argv[0], help );
				exit(1);
			}

		} else if (strcmp(p, "-y") == 0) {
			param++;
			if ((param < argc)&&(argv[param][0] != '-')) {
				g->height = strtol(argv[param], NULL, 10);
			} else {
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Not enough paramters for -y <window height>\n\n%s %s", argv[0], help );
				exit(1);
			}

		} else if (strcmp(p, "-z") == 0) {
			param++;
			if ((param < argc)&&(argv[param][0] != '-')) {
				g->font_size = strtof(argv[param], NULL);
			} else {
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Not enough paramters for -z <font size>\n\n%s %s", argv[0], help );
				exit(1);
			}

		} else if (strcmp(p, "-p") == 0) {
			param++;
			if ((param < argc)&&(argv[param][0] != '-')) {
				g->dpi = strtof(argv[param], NULL);
			} else {
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Not enough paramters for -p <dpi>\n\n%s %s", argv[0], help );
				exit(1);
			}

		} else if (strcmp(p, "-r") == 0) {
			param++;
			if ((param < argc)&&(argv[param][0] != '-')) {
				g->renderer = Renderers::get(atoi(argv[param]));
			} else {
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Not enough paramters for -r <render engine>\n\n%s %s", argv[0], help );
				exit(1);
			}

		} else if (strcmp(p, "-l") == 0) {
			g->slowCPU = true;

		} else if (strcmp(p, "-d") == 0) {
			g->debug = true;
#ifdef _WIN32
		} else if (!strncmp(p, "--reversesearch", 15)) {
			// Handling of DDE command for PDFBridge
			if ((argc - param - 1 < 2) || (argv[param + 1][0] == '-') || (argv[param + 2][0] == '-')) {
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Not enough paramters for --reversesearch <PDF path> <search string>\n\n%s %s", argv[0], help );
				exit(1);
			}

			g->pdfBridgePdfPath = argv[param + 1];
			g->pdfBridgeSearchStr = argv[param + 2];

			param += 2;
#endif
		} else if (argc == 2) {
			/*
			 * When we're using file-associations, the OS usually just
			 * passes the filename to be loaded as the single initial
			 * parameter, so in this special case situation we try to
			 * load it.
			 */
			g->input_file = argv[1];
			return 0;
		} else {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Unknown parameter '%s'\n\n%s %s", p, argv[0], help);
			exit(1);
		}
	}


	return 0;
}

void cleanupAndExit(int c) {
	if (window) SDL_DestroyWindow(window);
	SDL_Quit();
	exit(c);
}

int main(int argc, char **argv) {
	uint8_t sleepout;
	std::string configDir;
	globals g; // because some things we have to store *before* we load the config file in BoardView app.obvconf
	BoardView app{};

	// Log all messages
	SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);

	/*
	 * Parse the parameters first up, store the results in the global struct.
	 *
	 * This does mean a little more redundancy between the OS builds but not
	 * as bad as it was before.
	 *
	 */
	parse_parameters(argc, argv, &g);

	app.debug = g.debug;

	// Setup SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error: %s\n", SDL_GetError());
		return -1;
	}

#if SDL_VERSION_ATLEAST(2, 0, 10)
	// Enable touch gestures on multi-touch touchpads
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
#endif

	// Load the configuration file
	configDir = get_user_dir(UserDir::Config);
	if (!configDir.empty()) app.obvconfig.Load(configDir + "obv.conf", true);

	// Load file history
	std::string dataDir = get_user_dir(UserDir::Data);
	if (!dataDir.empty()) {
		app.fhistory.Set_filename(dataDir + "obv.history");
		app.fhistory.Load();
	}

	// If we've chosen to override the normally found config.
	if (g.config_file) app.obvconfig.Load(g.config_file, true);

#ifdef _WIN32
	// Run PDF reverse search command if called with --reversesearch
	if (g.pdfBridgePdfPath != nullptr && g.pdfBridgeSearchStr != nullptr) {
		PDFBridgeSumatra &pdfBrdigeSumatra = PDFBridgeSumatra::GetInstance(app.obvconfig);
		if (!pdfBrdigeSumatra.ReverseSearch(g.pdfBridgePdfPath, g.pdfBridgeSearchStr)) {
			return 2;
		} else {
			return 0;
		}
	}
#endif

	// Apply the slowCPU flag if required.
	app.config.slowCPU = g.slowCPU;

	if (g.width == 0) g.width   = app.config.windowX;
	if (g.height == 0) g.height = app.config.windowY;

	if (g.renderer == Renderers::Renderer::DEFAULT) {
		g.renderer = Renderers::get(app.obvconfig.ParseInt("renderer", static_cast<int>(Renderers::Preferred)));
	}

	// Setup window
	SDL_DisplayMode current;
	SDL_GetCurrentDisplayMode(0, &current);
	window = SDL_CreateWindow(
	    OBV_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, g.width, g.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create the sdlWindow: %s\n", SDL_GetError());
		cleanupAndExit(1);
	}

	// Needs to be done before initializing the renderer or using any of ImGui stuff
	ImGui::CreateContext();
	// Setup renderer
	bool initialized = Renderers::initBestRenderer(g.renderer, window);
	if (!initialized) {
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", "No renderer not available. Exiting.");
		cleanupAndExit(1);
	}

	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

	// SDL disables screen saver by default which doesn't make sense for us.
	SDL_EnableScreenSaver();

	ImGuiIO &io    = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable ImGui's keyboard navigation
	io.IniFilename = NULL;
	//	io.Fonts->AddFontDefault();

	// Main loop
	bool done             = false;
	bool preload_required = false;

	if (g.dpi > 0) setDPI(g.dpi);

	// Now that the configuration file is loaded in to BoardView, parse its settings.
	app.ConfigParse();

	// Preset some workable sizes
	app.m_board_surface.x = g.width;
	app.m_board_surface.y = g.height;
	if (app.config.showInfoPanel) app.m_board_surface.x -= app.m_info_surface.x;

	if (g.font_size > 0.0) app.config.fontSize = g.font_size;

	Fonts fonts;
	std::string loadedFontName = fonts.load(app.config.fontName, app.config.fontSize);
	if (!loadedFontName.empty()) { // Overwrite saved font name by the one that has just been loaded
		app.obvconfig.WriteStr("fontName", loadedFontName.c_str());
	}

	// ImVec4 clear_color = ImColor(20, 20, 30);
	ImVec4 clear_color = ImColor(app.m_colors.backgroundColor);

	/*
	 * If we've asked to load a file from the command line
	 * then this is where we stage it to be loaded directly
	 * in to OBV
	 */
	if (g.input_file) {
		preload_required = true;
	}

	/*
	 * The sleepout var keeps track of how many iterations of the main loop
	 * are left, without an event having happened before OBV will start to sleep
	 * and continue without redrawing the page.
	 *
	 * The reason we don't just sleep immediately after a non-event is because
	 * sometimes there are internal things that still need to be done on the next
	 * render (such as responding to a mouse click
	 *
	 * For now we've got this set to 3 frames which seems to work okay with OBV.
	 * If you find some things aren't working properly without you having to move
	 * the mouse or 'waking up' OBV then increase to 5 or more.
	 */
	sleepout = 30;
	float angleacc = 0.0;
	while (!done) {

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			sleepout = 30;
			Renderers::current->processEvent(event);

			if (event.type == SDL_DROPFILE) {
				app.LoadFile(filesystem::u8path(event.drop.file));
			} else if(event.type == SDL_MULTIGESTURE && event.mgesture.numFingers == 2 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				//Inhibit dragging board area
				app.m_dragging_token = -1;
				//Rotation detected, at least 1°
				if (fabs(event.mgesture.dTheta) > 3.14 / 180.0) {
					angleacc += event.mgesture.dTheta;
					if (angleacc >= 3.14 / 2) {
						// > 90°
						app.Rotate(1);
						angleacc = 0.0;
					} else if (angleacc <= -3.14 / 2) {
						// < 90°
						app.Rotate(-1);
						angleacc = 0.0;
					}
				}
				//Pinch-to-zoom
				else if (fabs(event.mgesture.dDist) > 0.002) {
					int w, h;
					SDL_GetWindowSize(window, &w, &h);
					app.Zoom(event.mgesture.x * w, event.mgesture.y * h, event.mgesture.dDist * app.config.zoomFactor * 10);
				}
			}

			if (event.type == SDL_QUIT) done = true;
		}

		// reset rotation angle accumulator
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			angleacc = 0.0;
		}

		if (app.reloadConfig) {
			app.reloadConfig = false;
			app.obvconfig.Load(configDir + "obv.conf");
			app.ConfigParse();
			clear_color = ImColor(app.m_colors.backgroundColor);
		}

		if (app.reloadFonts) {
			// Needs to happen after frame has been rendered (or before starting a new frame)
			fonts.reload(app.config.fontName, app.config.fontSize);
			app.reloadFonts = false;
		}

		if (!(sleepout--)) {
#ifdef _WIN32
			Sleep(50);
#else
			usleep(50000);
#endif
			sleepout = 0;
			continue;
		} // puts OBV to sleep if nothing is happening.
		// Prepare frame
		Renderers::current->initFrame();
		ImGui::NewFrame();

		// If we have a board to view being passed from command line, then "inject"
		// it here.
		if (preload_required) {
			app.LoadFile(filesystem::u8path(g.input_file));
			preload_required = false;
		}

		app.Update();
		if (app.m_wantsQuit) {
			SDL_Event sdlevent;
			sdlevent.type = SDL_QUIT;
			SDL_PushEvent(&sdlevent);
		}

		// Update the title of the SDL app if the board filename has changed. -
		// PLD20160618
		if (app.history_file_has_changed) {
			char scratch[1024];
			snprintf(scratch, sizeof(scratch), "%s - %s", OBV_NAME, app.fhistory.history[0]);
			SDL_SetWindowTitle(window, scratch);
			app.history_file_has_changed = 0;
		}

		// Render frame
		ImGui::Render();
		Renderers::current->renderFrame(clear_color);

		// vsync disabled, manual FPS limiting
		if (!SDL_GL_GetSwapInterval()) {
			static const int FPS = 30;
			static const std::chrono::duration<std::intmax_t, std::ratio<1, FPS>> frameDuration{1};
			static auto nextFrame = std::chrono::steady_clock::now() + frameDuration;

			std::this_thread::sleep_until(nextFrame);
			nextFrame += frameDuration;
		}
	}

	// Cleanup
	Renderers::current->shutdown();

	ImGui::DestroyContext();

	cleanupAndExit(0);
	return 0;
}
