/*
 *  Prints doubles the way MainWindow::saveMeshToFile does, through a real
 *  QTextStream, to check autoremesher-cli's OBJ writer against Qt.  It is not
 *  built by CMake: the toolbox that builds the CLI has a compiler but no Qt,
 *  and the host has Qt's runtime library but no compiler and no Qt headers.
 *  So it declares the few Qt symbols it needs itself, with signatures whose
 *  mangled names match Qt 6's, and links against the host's library:
 *
 *    toolbox run -c instant-meshes-build -- bash -c '
 *      g++ tests/manual/qtextstream_probe.cpp -o /tmp/qtextstream_probe \
 *        /run/host/usr/lib64/libQt6Core.so.6 -Wl,--allow-shlib-undefined &&
 *      LD_LIBRARY_PATH=/run/host/usr/lib64 /tmp/qtextstream_probe 123456.5 -0.0'
 *
 *  Each argument is read with strtod and printed on a `v` line.  Qt 6 only:
 *  the declarations below do not match Qt 5's ABI.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct QIODeviceBase {
    enum OpenModeFlag { ReadWrite = 3 };
};
template <typename T>
class QFlags {
public:
    int i;
};
// Qt 6's QString is a QArrayDataPointer: header, UTF-16 data, size.
class QString {
public:
    void* d;
    char16_t* ptr;
    long long size;
};
class QTextStream {
public:
    QTextStream(QString*, QFlags<QIODeviceBase::OpenModeFlag>);
    ~QTextStream();
    QTextStream& operator<<(double);
    QTextStream& operator<<(const char*);
    void flush();
    alignas(16) char storage[256]; // more than Qt's own object needs
};

int main(int argc, char** argv)
{
    QString text;
    std::memset(&text, 0, sizeof(text)); // a null QString
    {
        QFlags<QIODeviceBase::OpenModeFlag> mode;
        mode.i = QIODeviceBase::ReadWrite;
        QTextStream stream(&text, mode);
        for (int i = 1; i < argc; ++i)
            stream << "v " << std::strtod(argv[i], nullptr) << "\n";
        stream.flush();
    }
    for (long long i = 0; i < text.size; ++i)
        std::putchar(static_cast<char>(text.ptr[i]));
    return 0;
}
