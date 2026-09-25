# Writes the data package's size and SHA-256 as JSON (run by CMakeLists.txt after the
# game is linked, which writes the package): site/index.html checks the package against
# them and keeps it in Cache Storage under the hash.
#   cmake -DPACKAGE=dist/cortex.data -DOUTPUT=dist/cortex.data.json -P tools/data-package-json.cmake
file(SIZE "${PACKAGE}" size)
file(SHA256 "${PACKAGE}" sha256)
file(WRITE "${OUTPUT}" "{\"size\": ${size}, \"sha256\": \"${sha256}\"}\n")
