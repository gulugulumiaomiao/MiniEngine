#pragma once

#include <cstdint>
#include <string>

namespace engine {

class Engine;

struct AppConfig {
    std::string name{"Application"};
    std::uint32_t width{1280};
    std::uint32_t height{720};
    bool vsync{true};
};

class Application {
public:
    virtual ~Application() = default;

    [[nodiscard]] virtual AppConfig getConfig() const = 0;

protected:
    virtual void onStart() {}
    virtual void onUpdate(float deltaTime) { (void)deltaTime; }
    virtual void onStop() {}

private:
    friend class Engine;
};

} // namespace engine
