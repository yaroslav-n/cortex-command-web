-- Measurement only: records the simulation's state at fixed update counts, so two
-- runs (the original twice, or the original and the port) can be compared over
-- time. Nothing here draws random numbers or changes the game. UpdateScript runs
-- once per activity update, so the checkpoints are simulation steps, whatever the
-- frame rate.
local checkpoints = { [1] = true, [30] = true, [60] = true, [120] = true, [240] = true, [480] = true, [900] = true };
local lastCheckpoint = 900;

local function describe(object)
	return string.format("%s|%s|id %d|pos %.9g %.9g|vel %.9g %.9g|rot %.9g|angvel %.9g|", object.ClassName, object.PresetName, object.UniqueID, object.Pos.X, object.Pos.Y, object.Vel.X, object.Vel.Y, object.RotAngle, object.AngularVel);
end

function ParityTimelineScript:StartScript()
	self.step = 0;
	self.records = {};
end

function ParityTimelineScript:UpdateScript()
	self.step = self.step + 1;
	if not checkpoints[self.step] then
		return;
	end
	local lines = {};
	for actor in MovableMan.Actors do
		lines[#lines + 1] = describe(actor) .. string.format("health %.9g|status %d", actor.Health, actor.Status);
	end
	table.sort(lines);
	local items, particles = 0, 0;
	for item in MovableMan.Items do
		items = items + 1;
	end
	for particle in MovableMan.Particles do
		particles = particles + 1;
	end
	-- A sparse terrain sample: every 8th pixel of every 8th row, summed with
	-- positions so a moved hole shows up as well as a changed count.
	local sum, air = 0, 0;
	for y = 0, SceneMan.SceneHeight - 1, 8 do
		for x = 0, SceneMan.SceneWidth - 1, 8 do
			local material = SceneMan:GetTerrMatter(x, y);
			if material == 0 then
				air = air + 1;
			end
			sum = (sum * 31 + material * (x + 1) + y) % 2147483647;
		end
	end
	local records = self.records;
	records[#records + 1] = string.format("STEP %d actors %d items %d particles %d terrain %d air %d", self.step, #lines, items, particles, sum, air);
	records[#records + 1] = table.concat(lines, "\n");
	-- Written whole at the end, so the file's appearance means the run finished.
	if self.step == lastCheckpoint then
		local file = io.open("Userdata/ParityTimeline.txt", "wb");
		file:write(table.concat(records, "\n") .. "\n");
		file:close();
		print("PARITY TIMELINE WRITTEN");
		if jit then
			os.exit(0);
		end
	end
end
