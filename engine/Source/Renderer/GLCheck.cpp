#include "GLCheck.h"
#include "glad/gl.h"

#include <string>
#include <cstdio>
#include <sstream>
#include "ConsoleMan.h"
using namespace RTE;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

bool BrowserCheckEveryGLCall() {
	static const bool everyCall = MAIN_THREAD_EM_ASM_INT({ return new URLSearchParams(location.search).has('gl-check') ? 1 : 0; });
	return everyCall;
}
#endif

void CheckOpenGLError(const char* stmt, const char* fname, int line) {
	GLenum errorCode = glGetError();

	if (errorCode != GL_NO_ERROR) {
		std::string fileString = fname;
		std::string error = "Unknown error";
		std::string description = "No description";

		// Decode the error code
		switch (errorCode) {
			case GL_INVALID_ENUM: {
				error = "GL_INVALID_ENUM";
				description = "An unacceptable value has been specified for an enumerated argument.";
				break;
			}

			case GL_INVALID_VALUE: {
				error = "GL_INVALID_VALUE";
				description = "A numeric argument is out of range.";
				break;
			}

			case GL_INVALID_OPERATION: {
				error = "GL_INVALID_OPERATION";
				description = "The specified operation is not allowed in the current state.";
				break;
			}

#ifndef __EMSCRIPTEN__
			case GL_STACK_OVERFLOW: {
				error = "GL_STACK_OVERFLOW";
				description = "This command would cause a stack overflow.";
				break;
			}

			case GL_STACK_UNDERFLOW: {
				error = "GL_STACK_UNDERFLOW";
				description = "This command would cause a stack underflow.";
				break;
			}

#endif
			case GL_OUT_OF_MEMORY: {
				error = "GL_OUT_OF_MEMORY";
				description = "There is not enough memory left to execute the command.";
				break;
			}

			case GL_INVALID_FRAMEBUFFER_OPERATION: {
				error = "GL_INVALID_FRAMEBUFFER_OPERATION";
				description = "The object bound to FRAMEBUFFER_BINDING is not \"framebuffer complete\".";
				break;
			}

			default: {
				error = "Unknown Error" + std::to_string(errorCode);
				description = "";
			}
		}

		// Log the error
		std::stringstream errorMessage;
		errorMessage << "ERROR: An internal OpenGL call failed in "
		             << fileString.substr(fileString.find_last_of("\\/") + 1) << "(" << line << ")."
		             << "\nExpression:\n   " << stmt
		             << "\nError description:\n   " << error << "\n   " << description << "\n";
#ifdef __EMSCRIPTEN__
		// Keep GPU failures observable even before the in-game console is visible.
		static unsigned browserErrors = 0;
		if (browserErrors++ < 40) std::fprintf(stderr, "%s", errorMessage.str().c_str());
#endif
		g_ConsoleMan.PrintString(errorMessage.str());
	}
}
