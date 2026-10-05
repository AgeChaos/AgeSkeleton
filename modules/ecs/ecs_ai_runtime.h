#pragma once
#ifdef TOOLS_ENABLED
#include "core/object/ref_counted.h"
#include "core/error/error_macros.h"
#include "core/string/print_string.h"
#include "core/os/mutex.h"
#include "ecs_world.h"

// Development editor binary only. No Node objects or network listener.
class ECSAIRuntime : public RefCounted {
	String directory;
	Mutex mutex;
	Array logs;
	uint64_t sequence = 0;
	PrintHandlerList print_handler;
	ErrorHandlerList error_handler;
	void append(const String &level, const String &message);
	static void printed(void *, const String &, bool, bool);
	static void failed(void *, const char *, const char *, int, const char *, const char *, bool, ErrorHandlerType);
public:
	void start();
	void poll(const Ref<ECSWorld> &world, RID viewport, uint64_t frame);
	~ECSAIRuntime();
};
#endif
