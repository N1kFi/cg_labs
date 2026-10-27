#pragma once

#include "graphics_internal.hpp"

namespace application {

void setDemo(const char* name);
bool healthy();
void verificationStep(int frame);
bool initialize();
void shutdown();

void update(double time);
void render(const graphics::internal::FrameData& fd);

} // namespace application