#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kLogicalWidth = 160;
constexpr int kLogicalHeight = 152;
constexpr int kDefaultWindowScale = 3;
constexpr int kStageWidth = 6400;
constexpr int kStageHeight = 992;
constexpr float kCameraMinX = 64.0F;
constexpr float kCameraMinY = 64.0F;
constexpr float kCameraMaxX = static_cast<float>(kStageWidth - 224);
constexpr float kCameraMaxY = static_cast<float>(kStageHeight - 216);
constexpr float kPlayerStartX = 112.0F;
constexpr float kPlayerHalfWidth = 6.0F;
constexpr float kPlayerHalfHeight = 13.0F;
constexpr float kAcceleration = 520.0F;
constexpr float kDeceleration = 700.0F;
constexpr float kMaxRunSpeed = 115.0F;
constexpr float kGravity = 520.0F;
constexpr float kJumpSpeed = 230.0F;
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

struct Player {
    float x = kPlayerStartX;
    float y = 0.0F;
    float velocity_x = 0.0F;
    float velocity_y = 0.0F;
    bool grounded = false;
    bool facing_left = false;
};

struct CollisionMask {
    std::vector<unsigned char> pixels;

    bool solid(int x, int y) const {
        if (x < 0 || x >= kStageWidth || y < 0 || y >= kStageHeight) {
            return true;
        }
        return pixels[static_cast<std::size_t>(y) * kStageWidth + x] != 0;
    }

    int first_solid_y(int x, int start_y, int end_y) const {
        start_y = std::clamp(start_y, 0, kStageHeight - 1);
        end_y = std::clamp(end_y, 0, kStageHeight - 1);
        for (int y = start_y; y <= end_y; ++y) {
            if (solid(x, y)) {
                return y;
            }
        }
        return -1;
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

CollisionMask load_collision_mask(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::cerr << "Unable to load " << path << '\n';
        return {};
    }
    CollisionMask result;
    result.pixels.assign(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
    const auto expected =
        static_cast<std::size_t>(kStageWidth) * kStageHeight;
    if (result.pixels.size() != expected) {
        std::cerr << "Collision mask has " << result.pixels.size()
                  << " bytes; expected " << expected << '\n';
        result.pixels.clear();
    }
    return result;
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

void center_camera(float& camera_x, float& camera_y, const Player& player) {
    camera_x = std::clamp(
        player.x - 48.0F,
        kCameraMinX,
        kCameraMaxX);
    camera_y = std::clamp(
        player.y - 76.0F,
        kCameraMinY,
        kCameraMaxY);
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

float approach(float value, float target, float amount) {
    if (value < target) {
        return std::min(value + amount, target);
    }
    return std::max(value - amount, target);
}

void reset_player(Player& player, const CollisionMask& collision) {
    player = {};
    const int surface = collision.first_solid_y(
        static_cast<int>(kPlayerStartX), 0, kStageHeight - 1);
    player.y =
        static_cast<float>(surface >= 0 ? surface : 568) - kPlayerHalfHeight;
    player.grounded = true;
}

void update_player(Player& player, const CollisionMask& collision,
                   float movement, bool jump_pressed, float delta_seconds) {
    if (movement != 0.0F) {
        player.velocity_x += movement * kAcceleration * delta_seconds;
        player.velocity_x =
            std::clamp(player.velocity_x, -kMaxRunSpeed, kMaxRunSpeed);
        player.facing_left = movement < 0.0F;
    } else {
        player.velocity_x = approach(
            player.velocity_x, 0.0F, kDeceleration * delta_seconds);
    }

    if (jump_pressed && player.grounded) {
        player.velocity_y = -kJumpSpeed;
        player.grounded = false;
    }

    player.velocity_y += kGravity * delta_seconds;
    player.x += player.velocity_x * delta_seconds;
    player.x = std::clamp(
        player.x,
        kCameraMinX + kPlayerHalfWidth,
        kCameraMaxX + kLogicalWidth - kPlayerHalfWidth);

    const float previous_y = player.y;
    player.y += player.velocity_y * delta_seconds;
    const int left = static_cast<int>(std::floor(player.x - kPlayerHalfWidth));
    const int center = static_cast<int>(std::floor(player.x));
    const int right = static_cast<int>(std::floor(player.x + kPlayerHalfWidth));

    if (player.velocity_y >= 0.0F) {
        const int scan_start = static_cast<int>(
            std::floor(previous_y + kPlayerHalfHeight - 2.0F));
        const int scan_end = static_cast<int>(
            std::ceil(player.y + kPlayerHalfHeight + 3.0F));
        int surface = -1;
        for (const int sensor_x : {left, center, right}) {
            const int hit = collision.first_solid_y(
                sensor_x, scan_start, scan_end);
            if (hit >= 0 && (surface < 0 || hit < surface)) {
                surface = hit;
            }
        }
        if (surface >= 0) {
            player.y = static_cast<float>(surface) - kPlayerHalfHeight;
            player.velocity_y = 0.0F;
            player.grounded = true;
        } else {
            player.grounded = false;
        }
    }

    if (player.grounded) {
        int surface = -1;
        for (const int sensor_x : {left, center, right}) {
            const int hit = collision.first_solid_y(
                sensor_x,
                static_cast<int>(player.y + kPlayerHalfHeight - 5.0F),
                static_cast<int>(player.y + kPlayerHalfHeight + 7.0F));
            if (hit >= 0 && (surface < 0 || hit < surface)) {
                surface = hit;
            }
        }
        if (surface >= 0) {
            player.y = static_cast<float>(surface) - kPlayerHalfHeight;
        } else {
            player.grounded = false;
        }
    }
}

void update_camera(float& camera_x, float& camera_y, const Player& player,
                   float delta_seconds) {
    const float target_x = std::clamp(
        player.x - (player.facing_left ? 112.0F : 48.0F),
        kCameraMinX,
        kCameraMaxX);
    const float target_y = std::clamp(
        player.y - 76.0F, kCameraMinY, kCameraMaxY);
    camera_x = approach(camera_x, target_x, 150.0F * delta_seconds);
    camera_y = approach(camera_y, target_y, 150.0F * delta_seconds);
}

bool render_frame(Application& app, SDL_Texture* stage, SDL_Texture* collision,
                  SDL_Texture* sonic, const Player& player,
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

    SDL_FRect sonic_destination{
        std::floor(player.x - camera_x - 9.0F),
        std::floor(player.y - camera_y - 15.0F),
        18.0F,
        30.0F,
    };
    if (!SDL_RenderTextureRotated(
            app.renderer,
            sonic,
            nullptr,
            &sonic_destination,
            0.0,
            nullptr,
            player.facing_left ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE)) {
        return fail("Unable to render Sonic");
    }

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
    const auto collision_mask_path = data_directory / "collision-mask.bin";
    const auto sonic_path = data_directory / "sonic-idle.png";
    if (!std::filesystem::is_regular_file(stage_path) ||
        !std::filesystem::is_regular_file(collision_path) ||
        !std::filesystem::is_regular_file(collision_mask_path) ||
        !std::filesystem::is_regular_file(sonic_path)) {
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
    Texture sonic = load_png(app.renderer, sonic_path);
    CollisionMask collision_mask = load_collision_mask(collision_mask_path);
    if (stage.value == nullptr || collision.value == nullptr ||
        sonic.value == nullptr || collision_mask.pixels.empty()) {
        return 1;
    }

    Player player;
    reset_player(player, collision_mask);
    float camera_x = 0.0F;
    float camera_y = 0.0F;
    center_camera(camera_x, camera_y, player);

    if (smoke_test) {
        if (!render_frame(
                app, stage.value, collision.value, sonic.value, player,
                camera_x, camera_y, true)) {
            return 1;
        }
        std::cout << "Viewer smoke test passed using " << data_directory << '\n';
        return 0;
    }

    open_first_gamepad(app);
    bool running = true;
    bool show_collision = false;
    bool jump_pressed = false;
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
                        reset_player(player, collision_mask);
                        center_camera(camera_x, camera_y, player);
                    } else if (event.key.key == SDLK_SPACE ||
                               event.key.key == SDLK_Z) {
                        jump_pressed = true;
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
                        jump_pressed = true;
                    } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
                        show_collision = !show_collision;
                    } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                        reset_player(player, collision_mask);
                        center_camera(camera_x, camera_y, player);
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
        movement_x += keyboard[SDL_SCANCODE_RIGHT] || keyboard[SDL_SCANCODE_D];
        movement_x -= keyboard[SDL_SCANCODE_LEFT] || keyboard[SDL_SCANCODE_A];

        if (app.gamepad != nullptr) {
            movement_x += normalized_axis(SDL_GetGamepadAxis(
                app.gamepad, SDL_GAMEPAD_AXIS_LEFTX));
            movement_x += SDL_GetGamepadButton(
                app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            movement_x -= SDL_GetGamepadButton(
                app.gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        }

        update_player(
            player, collision_mask,
            std::clamp(movement_x, -1.0F, 1.0F),
            jump_pressed, delta_seconds);
        jump_pressed = false;
        update_camera(camera_x, camera_y, player, delta_seconds);

        const std::string title =
            "Sonic Pocket - Camera " +
            std::to_string(static_cast<int>(camera_x)) + ", " +
            std::to_string(static_cast<int>(camera_y)) + " - Sonic " +
            std::to_string(static_cast<int>(player.x)) + ", " +
            std::to_string(static_cast<int>(player.y)) +
            (show_collision ? " - Collision ON" : "");
        SDL_SetWindowTitle(app.window, title.c_str());

        if (!render_frame(
                app,
                stage.value,
                collision.value,
                sonic.value,
                player,
                camera_x,
                camera_y,
                show_collision)) {
            return 1;
        }
    }

    return 0;
}
