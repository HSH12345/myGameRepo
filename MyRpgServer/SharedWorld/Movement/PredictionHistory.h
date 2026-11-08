#pragma once
#include "../Core/CoordinateSystem.h"
#include <deque>
#include <stdexcept>

namespace shared_world
{
// Input and position deques are a single bounded history owned by the game
// thread. Matching authoritative acknowledgements need no collision replay.
template <class Input, class Move>
void ReconcilePrediction(std::deque<Input>& inputs, std::deque<WorldPosition>& positions, std::uint64_t sequence,
                         WorldPosition authoritative, WorldPosition& predicted, Move move)
{
    if (inputs.size() != positions.size())
    {
        throw std::logic_error("prediction history size mismatch");
    }

    const bool INTACT = !positions.empty() && predicted == positions.back();
    bool matched = false;
    while (!inputs.empty() && inputs.front().sequence <= sequence)
    {
        if (inputs.front().sequence == sequence)
        {
            matched = positions.front() == authoritative;
        }

        inputs.pop_front();
        positions.pop_front();
    }

    if (INTACT && matched)
    {
        return;
    }

    predicted = authoritative;
    std::size_t index = 0;
    for (const auto& input : inputs)
    {
        predicted = move(predicted, input.dx, input.dy);
        positions[index++] = predicted;
    }

}

}

