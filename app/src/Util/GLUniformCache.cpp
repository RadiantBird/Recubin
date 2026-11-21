#include <Util/GLUniformCache.hpp>
#include <GL/glew.h>

int cachedUniformLocation(unsigned int program, CachedUniform& slot, const char* name) {
    if (slot.program != program) {
        slot.program = program;
        slot.loc = glGetUniformLocation(program, name);
    }
    return slot.loc;
}
