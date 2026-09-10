#pragma once

namespace engine {

class Engine;

class Application {
public:
    virtual ~Application() = default;

protected:
    virtual void onStart() {}
    virtual void onUpdate(float deltaTime) { (void)deltaTime; }
    virtual void onStop() {}

private:
    friend class Engine;
};

} // namespace engine
