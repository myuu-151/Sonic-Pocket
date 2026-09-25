// sonic-pocket: plays the recompiled Sonic Pocket Adventure in an SDL3 window.
//
//   sonic-pocket <cartridge> [--interpreter] [--japanese] [--scale N]
//
// Controls: arrow keys or D-pad/left stick, Z / gamepad south = A,
// X / gamepad east = B, Enter / gamepad start = Option, Escape quits.
//
// The game's save data lives in the cartridge's flash chip (the last 32 KiB of
// the ROM); it is written to the SDL preferences folder whenever it changes.

#include "ngpc/machine.h"
#include "recomp_rt/game.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr size_t SAVE_OFFSET = 0x1F8000;   // cartridge offset of the flash save area
constexpr size_t SAVE_SIZE = 0x8000;
constexpr int AUDIO_CHANNELS = 2;
// Keep a few frames of sound queued; the emulation is paced by the audio clock.
constexpr int AUDIO_QUEUE_TARGET_BYTES = ngpc::Machine::AUDIO_RATE / 60 * AUDIO_CHANNELS * 2 * 4;

std::vector<uint8_t> read_file(const fs::path &path)
{
	std::ifstream file(path, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

fs::path save_path()
{
	char *pref = SDL_GetPrefPath("SonicPocket", "SonicPocket");
	fs::path path = pref ? fs::path(pref) / "flash_save.bin" : fs::path("flash_save.bin");
	SDL_free(pref);
	return path;
}

uint8_t read_buttons(const bool *keys, SDL_Gamepad *pad)
{
	uint8_t buttons = 0;
	auto pressed = [&](SDL_Scancode key, SDL_GamepadButton button) {
		return keys[key] || (pad && SDL_GetGamepadButton(pad, button));
	};
	if (pressed(SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP)) buttons |= ngpc::BUTTON_UP;
	if (pressed(SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) buttons |= ngpc::BUTTON_DOWN;
	if (pressed(SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) buttons |= ngpc::BUTTON_LEFT;
	if (pressed(SDL_SCANCODE_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) buttons |= ngpc::BUTTON_RIGHT;
	if (pressed(SDL_SCANCODE_Z, SDL_GAMEPAD_BUTTON_SOUTH)) buttons |= ngpc::BUTTON_A;
	if (pressed(SDL_SCANCODE_X, SDL_GAMEPAD_BUTTON_EAST)) buttons |= ngpc::BUTTON_B;
	if (pressed(SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_START)) buttons |= ngpc::BUTTON_OPTION;
	if (pad)
	{
		constexpr Sint16 dead_zone = 12000;
		const Sint16 x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
		const Sint16 y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
		if (x < -dead_zone) buttons |= ngpc::BUTTON_LEFT;
		if (x > dead_zone) buttons |= ngpc::BUTTON_RIGHT;
		if (y < -dead_zone) buttons |= ngpc::BUTTON_UP;
		if (y > dead_zone) buttons |= ngpc::BUTTON_DOWN;
	}
	return buttons;
}

} // namespace

int main(int argc, char **argv)
{
	fs::path rom_path;
	bool use_interpreter = false;
	int language = 1;
	int scale = 4;
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--interpreter") use_interpreter = true;
		else if (arg == "--japanese") language = 0;
		else if (arg == "--scale" && i + 1 < argc) scale = std::max(1, std::stoi(argv[++i]));
		else rom_path = arg;
	}
	if (rom_path.empty())
	{
		std::fprintf(stderr, "usage: sonic-pocket <cartridge> [--interpreter] [--japanese] [--scale N]\n");
		return 2;
	}

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
	{
		std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	std::vector<uint8_t> rom = read_file(rom_path);
	const fs::path save_file = save_path();
	if (rom.size() == 0x200000)
	{
		std::vector<uint8_t> save = read_file(save_file);
		if (save.size() == SAVE_SIZE)
			std::copy(save.begin(), save.end(), rom.begin() + SAVE_OFFSET);
	}

	auto machine = std::make_unique<ngpc::Machine>();
	if (const std::string error = machine->load_cartridge(rom); !error.empty())
	{
		std::fprintf(stderr, "cannot load %s: %s\n", rom_path.string().c_str(), error.c_str());
		return 1;
	}
	machine->boot(language);
	std::vector<uint8_t> saved(rom.begin() + SAVE_OFFSET, rom.begin() + SAVE_OFFSET + SAVE_SIZE);

	std::unique_ptr<rt::RecompiledGame> game;
	if (!use_interpreter)
		game = std::make_unique<rt::RecompiledGame>(*machine);

	SDL_Window *window = SDL_CreateWindow("Sonic Pocket Adventure",
			k2ge::SCREEN_WIDTH * scale, k2ge::SCREEN_HEIGHT * scale, SDL_WINDOW_RESIZABLE);
	SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
	if (!renderer)
	{
		std::fprintf(stderr, "cannot create window: %s\n", SDL_GetError());
		return 1;
	}
	SDL_SetRenderVSync(renderer, 0);
	SDL_SetRenderLogicalPresentation(renderer, k2ge::SCREEN_WIDTH, k2ge::SCREEN_HEIGHT,
			SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
	SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING, k2ge::SCREEN_WIDTH, k2ge::SCREEN_HEIGHT);
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

	const SDL_AudioSpec spec = { SDL_AUDIO_S16, AUDIO_CHANNELS, ngpc::Machine::AUDIO_RATE };
	SDL_AudioStream *audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
	if (audio)
		SDL_ResumeAudioStreamDevice(audio);
	else
		std::fprintf(stderr, "no audio: %s\n", SDL_GetError());

	SDL_Gamepad *pad = nullptr;
	const uint64_t frame_ns = 1000000000ull * 102485 / ngpc::Machine::CPU_CLOCK;  // ~59.95 Hz
	uint64_t next_frame = SDL_GetTicksNS();
	bool running = true;

	while (running)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT)
				running = false;
			else if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)
				running = false;
			else if (event.type == SDL_EVENT_GAMEPAD_ADDED && !pad)
				pad = SDL_OpenGamepad(event.gdevice.which);
			else if (event.type == SDL_EVENT_GAMEPAD_REMOVED && pad &&
					SDL_GetGamepadID(pad) == event.gdevice.which)
			{
				SDL_CloseGamepad(pad);
				pad = nullptr;
			}
		}

		machine->set_buttons(read_buttons(SDL_GetKeyboardState(nullptr), pad));
		if (game)
			game->run_frame();
		else
			machine->run_frame();

		std::vector<int16_t> &samples = machine->audio_samples();
		if (audio && !samples.empty())
			SDL_PutAudioStreamData(audio, samples.data(), static_cast<int>(samples.size() * sizeof(int16_t)));
		samples.clear();

		SDL_UpdateTexture(texture, nullptr, machine->framebuffer(), k2ge::SCREEN_WIDTH * 4);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		SDL_RenderTexture(renderer, texture, nullptr, nullptr);
		SDL_RenderPresent(renderer);

		// Persist the flash save area when the game has written to it.
		if (machine->frame_count() % 60 == 0)
		{
			const std::vector<uint8_t> &cart = machine->cartridge();
			if (!std::equal(saved.begin(), saved.end(), cart.begin() + SAVE_OFFSET))
			{
				saved.assign(cart.begin() + SAVE_OFFSET, cart.begin() + SAVE_OFFSET + SAVE_SIZE);
				std::ofstream(save_file, std::ios::binary)
					.write(reinterpret_cast<const char *>(saved.data()), static_cast<std::streamsize>(saved.size()));
			}
		}

		// Pace by the audio queue when there is sound, otherwise by the clock.
		if (audio)
		{
			while (running && SDL_GetAudioStreamQueued(audio) > AUDIO_QUEUE_TARGET_BYTES)
				SDL_Delay(1);
		}
		else
		{
			next_frame += frame_ns;
			const uint64_t now = SDL_GetTicksNS();
			if (next_frame > now)
				SDL_DelayNS(next_frame - now);
			else
				next_frame = now;
		}
	}

	game.reset();
	if (pad)
		SDL_CloseGamepad(pad);
	if (audio)
		SDL_DestroyAudioStream(audio);
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
