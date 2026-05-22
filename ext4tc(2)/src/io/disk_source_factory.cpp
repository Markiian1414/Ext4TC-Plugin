#include "disk_source.h"
#include "image_source.h"
#include "physical_source.h"
#include <string>
#include <algorithm>

// Factory: вибирає реалізацію за шляхом
// \\.\PhysicalDriveN або \\.\X: -> фізичний диск
// інакше -> образ файлу
std::unique_ptr<IDiskSource> CreateDiskSource(const std::string& path, bool readOnly) {
    bool isPhysical = (path.size() >= 4 &&
                       path[0] == '\\' && path[1] == '\\' &&
                       path[2] == '.'  && path[3] == '\\');
    if (isPhysical) {
        auto src = std::make_unique<PhysicalDiskSource>();
        if (src->Open(path, readOnly)) return src;
        return nullptr;
    } else {
        auto src = std::make_unique<ImageFileSource>();
        if (src->Open(path, readOnly)) return src;
        return nullptr;
    }
}
