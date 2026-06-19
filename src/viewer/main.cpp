#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr int kLogicalWidth = 160;
constexpr int kLogicalHeight = 152;
constexpr int kDefaultWindowScale = 3;
constexpr int kStageWidth = 6400;
constexpr int kStageHeight = 992;
constexpr float kPlayerStartX = 112.0F;
constexpr float kPlayerStartY = 424.0F;
constexpr float kCameraSpeed = 180.0F;
constexpr float kFastCameraSpeed = 480.0F;
constexpr Sint16 kGamepadDeadzone = 8000;

struct Texture {
    SDL_Texture* value = nullptr;

    Texture() = default;
    explicit Texture(SDL_Texture* texture) : value(texture) {}
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept : value(other.value) { other.value = nullptr; }
    Texture& operator=(Texture&& other) noexcept {
        if (this != &other) {
            SDL_DestroyTexture(value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    ~Texture() { SDL_DestroyTexture(value); }
};

struct Application {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Gamepad* gamepad = nullptr;

    ~Application() {
        SDL_CloseGamepad(gamepad);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

bool fail(std::string_view message) {
    std::cerr << message << ": " << SDL_GetError() << '\n';
    return false;
}

Texture load_png(SDL_Renderer* renderer, const std::filesystem::path& path,
                 bool black_is_transparent = false) {
    SDL_Surface* surface = SDL_LoadPNG(path.string().c_str());
    if (surface == nullptr) {
        fail("Unable to load " + path.string());
        return {};
    }

    if (black_is_transparent) {
        const Uint32 black = SDL_MapSurfaceRGB(surface, 0, 0, 0);
        if (!SDL_SetSurfaceColorKey(surface, true, black)) {
            fail("Unable to set collision transparency");
            SDL_DestroySurface(surface);
            return {};
        }
    }

    Texture texture{SDL_CreateTextureFromSurface(renderer, surface)};
    SDL_DestroySurface(surface);
    if (texture.value == nullptr) {
        fail("Unable to create texture for " + path.string());
        return {};
    }
    SDL_SetTextureScaleMode(texture.value, SDL_SCALEMODE_NEAREST);
    return texture;
}

std::filesystem::path find_data_directory(
    const std::filesystem::path& requested,
    const std::filesystem::path& executable) {
    if (!requested.empty()) {
        return requested;
    }

    const auto current = std::filesystem::current_path() / "out" / "nsi1";
    if (std::filesystem::is_directory(current)) {
        return current;
    }

    auto directory = std::filesystem::absolute(executable).parent_path();
    for (int depth = 0; depth < 5 && directory.has_parent_path(); ++depth) {
        const auto candidate = directory / "out" / "nsi1";
        if (std::filesystem::is_directory(candidate)) {
            return candidate;
        }
        directory = directory.parent_path();
    }
    return current;
}

void center_camera(float& camera_x, float& camera_y) {
    camera_x = std::clamp(
        kPlayerStartX - static_cast<float>(kLogicalWidth) / 2.0F,
        0.0F,
        static_cast<float>(kStageWidth - kLogicalWidth));
    camera_y = std::clamp(
        kPlayerStartY - static_cast<float>(kLogicalHeight) / 2.0F,
        0.0F,
        static_cast<float>(kStageHeight - kLogicalHeight));
}

bool set_window_scale(SDL_Window* window, int scale) {
    if (scale < 1 || scale > 6) {
        return true;
    }
    const int width = kLogicalWidth * scale;
    const int height = kLogicalHeight * scale;
    if (!SDL_SetWindowSize(window, width, height)) {
        return fail("Unable to set window size");
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return true;
}

float normalized_axis(Sint16 value) {
    if (std::abs(static_cast<int>(value)) < kGamepadDeadzone) {
        return 0.0F;
    }
    return static_cast<float>(value) / 32767.0F;
}

void open_gamepad(Application& app, SDL_JoystickID id) {
    if (app.gamepad != nullptr) {
        return;
    }
    app.gamepad = SDL_OpenGamepad(id);
    if (app.gamepad != nullptr) {
        std::cout << "Gamepad: " << SDL_GetGamepadName(app.gamepad) << '\n';
    }
}

void open_first_gamepad(Application& app) {
    int count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
    if (gamepads != nullptr && count > 0) {
        open_gamepad(app, gamepads[0]);
    }
    SDL_free(gamepads);
}

void draw_player_marker(SDL_Renderer* renderer, float x, float y) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 220);
    SDL_FRect shadow{x - 4.0F, y - 7.0F, 9.0F, 14.0F};
    SDL_RenderFillRect(renderer, &shadow);

    SDL_SetRenderDrawColor(renderer, 35, 95, 255, 255);
    SDL_FRect body{x - 3.0F, y - 6.0F, 7.0F, 11.0F};
    SDL_RenderFillRect(renderer, &body);

    SDL_SetRenderDrawColor(renderer, 255, 235, 40, 255);
    SDL_RenderLine(renderer, x - 5.0F, y, x + 5.0F, y);
    SDL_RenderLine(renderer, x, y - 8.0F, x, y + 8.0F);
}

bool render_frame(Application& app, SDL_Texture* stage, SDL_Texture* collision,
                  float camera_x, float camera_y, bool show_collision) {
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
    SDL_RenderClear(app.renderer);

    SDL_FRect source{
        std::floor(camera_x),
        std::floor(camera_y),
        static_cast<float>(kLogicalWidth),
        static_cast<float>(kLogicalHeight),
    };
    if (!SDL_RenderTexture(app.renderer, stage, &source, nullptr)) {
        return fail("Unable to render stage");
    }

    if (show_collision) {
        SDL_SetTextureAlphaMod(collision, 160);
        if (!SDL_RenderTexture(app.renderer, collision, &source, nullptr)) {
            return fail("Unable to render collision overlay");
        }
    }

    draw_player_marker(
        app.renderer,
        kPlayerStartX - camera_x,
        kPlayerStartY - camera_y);

    SDL_RenderPresent(app.renderer);
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    bool smoke_test = false;
    std::filesystem::path requested_data;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--smoke-test") {
            smoke_test = true;
        } else if (requested_data.empty()) {
            requested_data = argv[index];
        } else {
            std::cerr << "Usage: sonic-pocket-viewer [data-directory] [--smoke-test]\n";
            return 2;
        }
    }

    const auto data_directory =
        find_data_directory(requested_data, argc > 0 ? argv[0] : "");
    const auto stage_path = data_directory / "stage.png";
    const auto collision_path = data_directory / "collision.png";
    if (!std::filesystem::is_regular_file(stage_path) ||
        !std::filesystem::is_regular_file(collision_path)) {
        std::cerr << "Missing extracted stage data in " << data_directory << '\n'
                  << "Run: py -3 tools/extract_level.py\n";
        return 2;
    }

    Application app;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fail("Unable to initialize SDL");
        return 1;
    }

    const SDL_WindowFlags flags =
        smoke_test ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE;
    if (!SDL_CreateWindowAndRenderer(
            "Sonic Pocket - Native Stage Viewer",
            kLogicalWidth * kDefaultWindowScale,
            kLogicalHeight * kDefaultWindowScale,
            flags,
            &app.window,
            &app.renderer)) {
        fail("Unable to create the SDL window");
        return 1;
    }
    if (!SDL_SetRenderLogicalPresentation(
            app.renderer,
            kLogicalWidth,
            kLogicalHeight,
            SDL_LOGICAL_PRESENTATION_INTEGER_SCALE)) {
        fail("Unable to set integer-scaled logical presentation");
        return 1;
    }
    SDL_SetRenderVSync(app.renderer, 1);

    Texture stage = load_png(app.renderer, stage_path);
    Texture collision = load_png(app.renderer, collision_path, true);
    if (stage.value == nullptr || collision.value == nullptr) {
        return 1;
    }

    float camera_x = 0.0F;
    float camera_y = 0.0F;
    center_camera(camera_x, camera_y);

    if (smoke_test) {
        if (!render_frame(
                app, stage.value, collision.value, camera_x, camera_y, true)) {
            return 1;
        }
        std::cout << "Viewer smoke test passed using " << data_directory << '\n';
        return 0;
    }

    open_first_gamepad(app);
    bool running = true;
    bool show_collision = false;
    Uint64 previous_ticks = SDL_GetTicks();

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.repeat) {
                        break;
                    }
                    if (event.key.key == SDLK_ESCAPE) {
                        running = false;
                    } else if (event.key.key == SDLK_C) {
                        show_collision = !show_collision;
                    } else if (event.key.key == SDLK_R ||
                               event.key.key == SDLK_HOME) {
                        center_camera(camera_x, camera_y);
                    } else if (event.key.key >= SDLK_1 &&
                               event.key.key <= SDLK_6) {
                        if (!set_window_scale(
                                app.window,
                                static_cast<int>(event.key.key - SDLK_0))) {
                            return 1;
                        }
                    }
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    open_gamepad(app, event.gdevice.which);
                    break;
                case SDL_EVENT_GAMEPAD_REMOVED:
                    if (app.gamepad != nullptr &&
                        SDL_GetGamepadID(app.gamepad) == event.gdevice.which) {
                        SDL_CloseGamepad(app.gamepad);
                        app.gamepad = nullptr;
                    }
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                        show_collision = !show_collision;
                    } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                        center_camera(camera_x, camera_y);
                    }
                    break;
                default:
                    break;
            }
        }

        const Uint64 current_ticks = SDL_GetTicks();
        const float delta_seconds = std::min(
            static_cast<float>(current_ticks - previous_ticks) / 1000.0F,
            0.05F);
        previous_ticks = current_ticks;

        const bool* keyboard = SDL_GetKeyboardState(nullptr);
        float movement_x = 0.0F;
        float movement_y = 0.0F;
        movement_x += keyboard[SDL_SCANCODE_RIGHT] || keyboard[SDL_SCANCODE_D];
        movement_x -= keyboard[SDL_SCANCODE_LEFT] || keyboard[SDL_SCANCODE_A];
        movement_y += keyboard[SDL_SCANCODE_DOWN] || keyboard[SDL_SCANCODE_S];
        movement_y -= keyboard[SDL_SCANCODE_UP] || keyboard[SDL_SCANCODE_W];

        if (app.gamepad != nullptr) {
            movement_x += normalized_axis(SDL_GetGamepadAxis(
                app.gamepad, SDL_GAMEPAD_AXIS_LEFTX));
            movement_y += normalized_axis(SDL_GetGamepadAxis(
                app.gamepad, SDL_GAMEPAD_AXIS_LEFTY));
            movement_x += SDL_GetGamepadButton(
                app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            movement_x -= SDL_GetGamepadButton(
                app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            movement_y += SDL_GetGamepadButton(
                app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            movement_y -= SDL_GetGamepadButton(
                app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
        }

        const bool fast =
            keyboard[SDL_SCANCODE_LSHIFT] || keyboard[SDL_SCANCODE_RSHIFT];
        const float speed = fast ? kFastCameraSpeed : kCameraSpeed;
        camera_x = std::clamp(
            camera_x + movement_x * speed * delta_seconds,
            0.0F,
            static_cast<float>(kStageWidth - kLogicalWidth));
        camera_y = std::clamp(
            camera_y + movement_y * speed * delta_seconds,
            0.0F,
            static_cast<float>(kStageHeight - kLogicalHeight));

        const std::string title =
            "Sonic Pocket - Camera " +
            std::to_string(static_cast<int>(camera_x)) + ", " +
            std::to_string(static_cast<int>(camera_y)) +
            (show_collision ? " - Collision ON" : "");
        SDL_SetWindowTitle(app.window, title.c_str());

        if (!render_frame(
                app,
                stage.value,
                collision.value,
                camera_x,
                camera_y,
                show_collision)) {
            return 1;
        }
    }

    return 0;
}
