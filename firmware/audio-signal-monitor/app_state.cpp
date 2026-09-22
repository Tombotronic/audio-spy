#include "app_state.h"

AppState g_state;

void appStateInit() {
    g_state.mutex = xSemaphoreCreateMutex();
}
