#include "obj_io.h"
#include <exception>
#include <fstream>
#include <sstream>

namespace AutoRemesherCli {

bool readObj(const std::string& path, std::vector<double>& vertices,
    std::vector<uint32_t>& triangles, std::string& error)
{
    std::ifstream in(path);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        std::istringstream stream(line);
        std::string tag;
        stream >> tag;
        if ("v" == tag) {
            double x, y, z;
            if (!(stream >> x >> y >> z)) {
                error = path + ":" + std::to_string(lineNumber) + ": bad vertex";
                return false;
            }
            vertices.insert(vertices.end(), { x, y, z });
        } else if ("f" == tag) {
            std::vector<uint32_t> face;
            std::string corner;
            // "7", "7/1" and "7/1/3" all start with the 1-based vertex index.
            try {
                while (stream >> corner)
                    face.push_back(static_cast<uint32_t>(std::stoul(corner) - 1));
            } catch (const std::exception&) {
                error = path + ":" + std::to_string(lineNumber) + ": bad face index \"" + corner
                    + "\"";
                return false;
            }
            for (size_t i = 1; i + 1 < face.size(); ++i)
                triangles.insert(triangles.end(), { face[0], face[i], face[i + 1] });
        }
    }
    if (in.bad()) {
        error = "cannot read " + path;
        return false;
    }
    return true;
}

bool writeObj(const std::string& path, const autoremesher_core_result& mesh, std::string& error)
{
    std::ofstream out(path);
    if (!out) {
        error = "cannot write " + path;
        return false;
    }
    out << "# autoremesher-cli\n";
    for (size_t i = 0; i < mesh.vertex_count; ++i) {
        out << "v " << mesh.vertices[i * 3] << " " << mesh.vertices[i * 3 + 1] << " "
            << mesh.vertices[i * 3 + 2] << "\n";
    }
    size_t index = 0;
    for (size_t i = 0; i < mesh.face_count; ++i) {
        out << "f";
        for (uint32_t j = 0; j < mesh.face_sizes[i]; ++j)
            out << " " << (1 + mesh.face_indices[index++]);
        out << "\n";
    }
    out.close();
    if (!out) {
        error = "cannot write " + path;
        return false;
    }
    return true;
}

}
