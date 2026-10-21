#pragma once
#include <cstdio>
template<typename... Args> inline void test_log(const char *, const char *, Args...) {}
#define ESP_LOGV(...) test_log(__VA_ARGS__)
#define ESP_LOGD(...) test_log(__VA_ARGS__)
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) test_log(__VA_ARGS__)
#define YESNO(x) ((x)?"YES":"NO")
#define ONOFF(x) ((x)?"ON":"OFF")
