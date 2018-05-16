#pragma once

#include "messages/image.hpp"
#include "util/concurrentmap.hpp"

namespace chatterino {
namespace util {

struct EmoteData {
    EmoteData() = default;

    EmoteData(messages::Image *_image)
        : image1x(_image)
    {
    }

    // Emotes must have a 1x image to be valid
    bool isValid() const
    {
        return this->image1x != nullptr;
    }

    messages::Image *image1x = nullptr;
    messages::Image *image2x = nullptr;
    messages::Image *image3x = nullptr;

    // Link to the emote page i.e. https://www.frankerfacez.com/emoticon/144722-pajaCringe
    QString pageLink;
};

using EmoteMap = ConcurrentMap<QString, EmoteData>;

}  // namespace util
}  // namespace chatterino
