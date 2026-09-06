#include "runtime/application/GameApplication.h"
#include "runtime/engine/Engine.h"

int main() {
    engine::GameApplication application;
    return ENGINE.run(application);
}
