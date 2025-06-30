#include "cler.hpp"
#include "result.hpp"
#include "utils.hpp"
#include <thread>
#include <chrono>
#include <SDL2/SDL.h>

const size_t CHANNEL_SIZE = 512;
const size_t BATCH_SIZE = CHANNEL_SIZE / 2;

struct SourceBlock : public cler::BlockBase<SourceBlock> {
    SourceBlock(const char* name)  : BlockBase(name) {} 

    cler::Result<cler::Empty, ClerError> procedure_impl(
        cler::Channel<float>* out) {
        if (out->space() < BATCH_SIZE) {
            return ClerError::NotEnoughSpace;
        }
        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            out->push(i % BATCH_SIZE);
        }
        return cler::Empty{};
    }
};

struct FreqPlotBlock : public cler::BlockBase<FreqPlotBlock> {
    cler::Channel<float> in;

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    static constexpr int WINDOW_WIDTH = 800;
    static constexpr int WINDOW_HEIGHT = 400;

    FreqPlotBlock(const char* name)
        : BlockBase(name), in(CHANNEL_SIZE) 
    {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            throw std::runtime_error("SDL_Init failed!");
        }

        window = SDL_CreateWindow(
            "FreqPlot",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            WINDOW_WIDTH,
            WINDOW_HEIGHT,
            SDL_WINDOW_SHOWN
        );

        if (!window) {
            throw std::runtime_error("SDL_CreateWindow failed!");
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer) {
            throw std::runtime_error("SDL_CreateRenderer failed!");
        }
    }

    ~FreqPlotBlock() {
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    cler::Result<cler::Empty, ClerError> procedure_impl() {
        if (in.size() < BATCH_SIZE) {
            return ClerError::NotEnoughSamples;
        }

        // Pull batch
        std::vector<float> samples(BATCH_SIZE);
        for (size_t i = 0; i < BATCH_SIZE; ++i) {
            float val;
            in.pop(val);
            samples[i] = val;
        }

        // Clear window
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // Draw waveform
        SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
        for (size_t i = 1; i < BATCH_SIZE; ++i) {
            int x1 = (i - 1) * WINDOW_WIDTH / BATCH_SIZE;
            int y1 = WINDOW_HEIGHT / 2 - static_cast<int>(samples[i - 1]);
            int x2 = i * WINDOW_WIDTH / BATCH_SIZE;
            int y2 = WINDOW_HEIGHT / 2 - static_cast<int>(samples[i]);
            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        }

        SDL_RenderPresent(renderer);

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        

        return cler::Empty{};
    }
};

int main() {
    SourceBlock source("Source");
    FreqPlotBlock freqplot("FreqPlot");

    cler::BlockRunner source_runners{&source, &freqplot.in};
    cler::BlockRunner freqplot_runners{&freqplot};

    cler::FlowGraph flowgraph(
        source_runners,
        freqplot_runners
    );

    flowgraph.run();


    while (true) {
        // Simulate some work in the main thread
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}