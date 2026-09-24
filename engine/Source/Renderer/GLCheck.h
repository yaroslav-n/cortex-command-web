#pragma once

/// Debug function to print GL errors to the console from:
/// https : // stackoverflow.com/questions/11256470/define-a-macro-to-facilitate-opengl-command-debugging
void CheckOpenGLError(const char* stmt, const char* fname, int line);

#ifdef __EMSCRIPTEN__
/// Whether every GL call is followed by an error check (the page's ?gl-check).
/// In Chrome glGetError is a synchronous round trip to the GPU process, and after
/// every call it cost more of a mission frame than the whole MovableMan update, so
/// by default WindowMan::UploadFrame checks once per frame instead.
bool BrowserCheckEveryGLCall();

/// Debug macro to be used for all GL calls.
#define GL_CHECK(stmt) \
	do { \
		stmt; \
		if (BrowserCheckEveryGLCall()) { \
			CheckOpenGLError(#stmt, __FILE__, __LINE__); \
		} \
	} while (0)
#else
/// Debug macro to be used for all GL calls.
#define GL_CHECK(stmt) \
	do { \
		stmt; \
		CheckOpenGLError(#stmt, __FILE__, __LINE__); \
	} while (0)
#endif
