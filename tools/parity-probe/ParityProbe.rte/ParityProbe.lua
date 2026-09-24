-- Measurement only: reads the terrain the game generated and writes it out, so the
-- original game and the browser port can be compared without changing either's
-- code. StartScript runs inside GAScripted::Start, after Activity::Start has
-- reseeded the generator and loaded the Scene, before any simulation step.
function ParityProbeScript:StartScript()
	local width = SceneMan.SceneWidth;
	local height = SceneMan.SceneHeight;
	local file = io.open("Userdata/ParityProbe.bin", "wb");
	file:write(string.format("%d %d\n", width, height));
	for y = 0, height - 1 do
		local row = {};
		for x = 0, width - 1 do
			row[x + 1] = string.char(SceneMan:GetTerrMatter(x, y));
		end
		file:write(table.concat(row));
	end
	-- The master Lua state's own random stream, which mission and global scripts
	-- draw from: SelectRand and PosRand go through LuaMan's per-state generator.
	local draws = {};
	for i = 1, 32 do
		draws[#draws + 1] = tostring(SelectRand(0, 1000000));
	end
	for i = 1, 8 do
		draws[#draws + 1] = string.format("%.9g", PosRand());
	end
	file:write("\nLUA " .. table.concat(draws, " ") .. "\n");
	file:close();
	print("PARITY PROBE WRITTEN " .. width .. "x" .. height);
	-- LuaJIT defines `jit`; the browser's Lua 5.1 does not. Only the native game
	-- quits here — the browser keeps running so its storage can flush the file.
	if jit then
		os.exit(0);
	end
end

function ParityProbeScript:UpdateScript()
end
