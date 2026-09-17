local generators = {
    vs2026 = "Visual Studio 18 2026",
    vs2022 = "Visual Studio 17 2022",
}

function buildCMakeDependency(directory, options)
    local generator = generators[_ACTION]
    if not generator then
        error("Use GenerateProject.bat to select Visual Studio 2026 or 2022.")
    end
    local cmake = os.getenv("BAAMBOO_CMAKE") or "cmake"
    local instance = os.getenv("BAAMBOO_VS_INSTANCE")
    local source = path.getabsolute(directory)
    local cacheFile = io.open(path.join(source, "CMakeCache.txt"), "r")
    local fresh = ""
    if cacheFile then
        local cache = cacheFile:read("*a")
        cacheFile:close()
        local oldGenerator = cache:match("CMAKE_GENERATOR:INTERNAL=([^\r\n]+)")
        local oldInstance = cache:match("CMAKE_GENERATOR_INSTANCE:[^=\r\n]+=([^\r\n]+)")
        local function normalize(value)
            return (value or ""):gsub("\\", "/"):gsub("/+$", ""):lower()
        end
        if oldGenerator ~= generator or (instance and normalize(oldInstance) ~= normalize(instance)) then
            fresh = " --fresh"
        end
    end
    local command = string.format('"%s" -S "%s" -B "%s" -G "%s" -A x64%s', cmake, source, source, generator, fresh)
    if instance then
        command = command .. string.format(' -DCMAKE_GENERATOR_INSTANCE="%s"', instance)
    end
    command = command .. " " .. (options or "")
    local configured = os.execute('"' .. command .. '"')
    if configured ~= true and configured ~= 0 then
        error(directory .. ": CMake configuration failed.")
    end
    local build = string.format('"%s" --build "%s" --config Release', cmake, source)
    local built = os.execute('"' .. build .. '"')
    if built ~= true and built ~= 0 then
        error(directory .. ": build failed.")
    end
    print(directory .. " generated!")
end
