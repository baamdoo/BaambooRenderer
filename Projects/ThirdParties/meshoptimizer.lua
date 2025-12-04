os.execute([["cd meshoptimizer & cmake . -G "Visual Studio 17 2022" -A x64 -S . -B ."]])
-- os.execute([["cd glfw & cmake --build "." --config debug"]])
os.execute([["cd meshoptimizer & cmake --build "." --config release"]])

printf("meshoptimizer generated!\n")