#pragma once

#include "runtime/application/Application.h"

namespace engine {

class GameApplication final : public Application {
public:
    [[nodiscard]] AppConfig getConfig() const override;

protected:
    void onStart() override;
    void onUpdate(float deltaTime) override;
    void onStop() override;
};

} // namespace engine
