-- Measurement: ends a mission the way a battle ends — by destroying brains — and
-- records how the activity responds, so the original and the port can be compared
-- on their win and loss handling. When a mission ends, GAScripted::End calls every
-- global script's EndScript and then deletes them, so the record is written there,
-- or at lastStep if the mission keeps running.
--
-- These are ordinary (not late-update) global scripts on purpose. Gibbing makes
-- gibs, and gibs with scripts take a threaded Lua state with try_lock. During the
-- late update the asynchronous Lua garbage collection MovableMan has just started
-- may hold that state, the lock fails, and the original stops on an assertion
-- dialog (LuaMan::GetAndLockFreeScriptState) — an upstream race, not something a
-- measurement should provoke.
--   ParityDefeatScript  gibs every human player's brain at step 60.
--   ParityVictoryScript gibs every actor that is not on a human player's team.
-- The engine creates only the enabled script's class table, so make sure both
-- exist before this shared file defines methods on them.
ParityDefeatScript = ParityDefeatScript or {};
ParityVictoryScript = ParityVictoryScript or {};

local actStep = 60;
local lastStep = 600;

local function humanTeams(activity)
	local teams = {};
	for player = 0, 3 do
		if activity:PlayerActive(player) and activity:PlayerHuman(player) then
			teams[activity:GetTeamOfPlayer(player)] = true;
		end
	end
	return teams;
end

local function teamCounts()
	local counts = {};
	for actor in MovableMan.Actors do
		counts[actor.Team] = (counts[actor.Team] or 0) + 1;
	end
	local parts = {};
	for team = -1, 3 do
		if counts[team] then
			parts[#parts + 1] = string.format("team %d: %d", team, counts[team]);
		end
	end
	return table.concat(parts, ", ");
end

local finish;

local function start(self)
	self.step = 0;
	self.records = {};
	self.lastState = nil;
end

local function update(self, act)
	self.step = self.step + 1;
	local activity = ActivityMan:GetActivity();
	if self.step == actStep then
		self.records[#self.records + 1] = string.format("STEP %d before: %s", self.step, teamCounts());
		act(self, activity);
	end
	local state = string.format("state %d over %s winner %d", activity.ActivityState, tostring(activity:IsOver()), ToGameActivity(activity).WinnerTeam);
	if state ~= self.lastState then
		self.records[#self.records + 1] = string.format("STEP %d %s", self.step, state);
		self.lastState = state;
	end
	if self.step == lastStep then
		finish(self, "still running");
	end
end

-- Writes the record once, then quits the native game (LuaJIT defines `jit`; the
-- browser keeps running so its storage can flush the file).
finish = function(self, reason)
	if self.finished then
		return;
	end
	self.finished = true;
	local activity = ActivityMan:GetActivity();
	self.records[#self.records + 1] = string.format("STEP %d %s: state %d over %s winner %d; %s", self.step, reason, activity.ActivityState, tostring(activity:IsOver()), ToGameActivity(activity).WinnerTeam, teamCounts());
	local file = io.open("Userdata/ParityEndgame.txt", "wb");
	file:write(table.concat(self.records, "\n") .. "\n");
	file:close();
	print("PARITY ENDGAME WRITTEN");
	if jit then
		os.exit(0);
	end
end;

function ParityDefeatScript:StartScript()
	start(self);
end

function ParityDefeatScript:EndScript()
	finish(self, "activity ended");
end

function ParityDefeatScript:UpdateScript()
	update(self, function(self, activity)
		for player = 0, 3 do
			if activity:PlayerActive(player) and activity:PlayerHuman(player) then
				local brain = activity:GetPlayerBrain(player);
				if brain then
					self.records[#self.records + 1] = string.format("GIB player %d brain %s", player, brain.PresetName);
					brain:GibThis();
				else
					self.records[#self.records + 1] = string.format("NO BRAIN for player %d", player);
				end
			end
		end
	end);
end

function ParityVictoryScript:StartScript()
	start(self);
end

function ParityVictoryScript:EndScript()
	finish(self, "activity ended");
end

function ParityVictoryScript:UpdateScript()
	update(self, function(self, activity)
		local friendly = humanTeams(activity);
		local victims = {};
		for actor in MovableMan.Actors do
			if not friendly[actor.Team] and not actor:IsInGroup("Doors") then
				victims[#victims + 1] = actor;
			end
		end
		self.records[#self.records + 1] = string.format("GIB %d enemy actors", #victims);
		for _, actor in ipairs(victims) do
			actor:GibThis();
		end
	end);
end
