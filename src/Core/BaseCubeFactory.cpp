#include <Core/BaseCubeFactory.hpp>

#include <Instances/Cube.hpp>
#include <Instances/Cylinder.hpp>
#include <Instances/TriangularPrism.hpp>
#include <Instances/Truss.hpp>
#include <Instances/Seat.hpp>
#include <Instances/Sphere.hpp>
#include <Instances/MeshCube.hpp>
#include <Instances/LiquidCube.hpp>
#include <Instances/SpawnLocation.hpp>
#include <Instances/Skybox.hpp>
#include <Instances/Sun.hpp>
#include <Instances/Moon.hpp>

std::shared_ptr<Instance> createBaseCubeInstance(std::string_view className) {
    if (className == "Cube")
        return std::make_shared<Cube>(Vector3(0, 0, 0), Vector3(1, 1, 1), 0);
    if (className == "Cylinder")
        return std::make_shared<Cylinder>(Vector3(0, 0, 0), Vector3(1, 1, 1));
    if (className == "TriangularPrism")
        return std::make_shared<TriangularPrism>(Vector3(0, 0, 0), Vector3(1, 1, 1));
    if (className == "Truss")
        return std::make_shared<Truss>(Vector3(0, 0, 0), Vector3(1, 1, 1), 0);
    if (className == "Seat")
        return std::make_shared<Seat>(Vector3(0, 0, 0), Vector3(1, 1, 1), 0);
    if (className == "Sphere")
        return std::make_shared<Sphere>(Vector3(0, 0, 0), Vector3(1, 1, 1));
    if (className == "MeshCube")
        return std::make_shared<MeshCube>(Vector3(0, 0, 0), Vector3(1, 1, 1));
    if (className == "LiquidCube")
        return std::make_shared<LiquidCube>(Vector3(0, 0, 0), Vector3(4, 2, 4));
    if (className == "SpawnLocation") return std::make_shared<SpawnLocation>();
    if (className == "Skybox") return std::make_shared<Skybox>();
    if (className == "Sun") return std::make_shared<Sun>();
    if (className == "Moon") return std::make_shared<Moon>();
    return nullptr;
}
