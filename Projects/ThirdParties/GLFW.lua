os.execute([["cd glfw & cmake . -G "Visual Studio 17 2022" -A x64 -S . -B ."]])
-- os.execute([["cd glfw & cmake --build "." --config debug"]])
os.execute([["cd glfw & cmake --build "." --config release"]])

printf("GLFW generated!\n")