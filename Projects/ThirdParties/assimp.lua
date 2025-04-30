filepath = path.join(os.getcwd(), "assimp/CMakeLists.txt")
print(filepath)
local lines = {}
for line in io.lines(filepath) do
    table.insert(lines, line)
end

f = io.open(filepath, "r")
local target_line = 1
for line in f:lines() do
    if line:match("ASSIMP_WARNINGS_AS_ERRORS") then
        target_line = target_line + 2
        break
    end
    target_line = target_line + 1
end
f:close()

if lines[target_line] then lines[target_line] = "  OFF" end

file = io.open(filepath, "w")
    for _, line in ipairs(lines) do
        file:write(line .. "\n")
    end
file:close()

os.execute([["cd assimp & cmake . -G "Visual Studio 17 2022" -A x64 -S . -B ."]])
-- os.execute([["cd assimp & cmake --build "." --config debug"]])
os.execute([["cd assimp & cmake --build "." --config release"]])

printf("Assimp generated!\n")