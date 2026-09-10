#pragma once

#include "runtime/application/Application.h"

namespace engine {

class GameApplication final : public Application {
protected:
    void onStart() override;
    void onUpdate(float deltaTime) override;
    void onStop() override;
};

} // namespace engine
