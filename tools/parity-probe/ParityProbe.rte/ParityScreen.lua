-- Measurement: saves the composed screen at a fixed simulation step through the
-- game's own screenshot function, so the original's rendering can be compared
-- with the port's without a display. The picture is the last frame drawn at or
-- before that step.
--
-- The camera glides towards its target on real time, not simulation steps, so two
-- builds running at different speeds frame different views. From pinStep on this
-- late-update script (which runs after the activity has set its own target) pins
-- screen 0's target and offset to where the player's brain first appeared — its
-- spawn point, identical in both builds even when the brain is an actor that then
-- walks — so both builds draw from the same camera position. A marker file is written a second after the shot, once
-- the screenshot (saved on a background thread) has had time to land.
local pinStep = 200;
local shotStep = 300;
local markerStep = 360;

function ParityScreenScript:StartScript()
	self.step = 0;
end

function ParityScreenScript:UpdateScript()
	self.step = self.step + 1;
	if not self.pin then
		local brain = ActivityMan:GetActivity():GetPlayerBrain(0);
		if brain then
			self.pin = Vector(math.floor(brain.Pos.X), math.floor(brain.Pos.Y));
		elseif self.step >= pinStep then
			self.pin = Vector(math.floor(SceneMan.SceneWidth / 2), math.floor(SceneMan.SceneHeight / 2));
		end
	end
	if self.step >= pinStep and self.step <= shotStep then
		-- The camera aims half the screen occlusion (a build-phase picker, say) away
		-- from its target, so the target carries that half back; and shake, which
		-- decays on real time, is cancelled (the camera clamps it at zero).
		local occlusion = CameraMan:GetScreenOcclusion(0);
		CameraMan:SetScrollTarget(Vector(self.pin.X + occlusion.X / 2, self.pin.Y + occlusion.Y / 2), 0.1, 0);
		CameraMan:SetScroll(self.pin, 0);
		CameraMan:AddScreenShake(-100000, 0);
	end
	if self.step == shotStep then
		FrameMan:SaveScreenToPNG("ParityScreen");
	elseif self.step == markerStep then
		local file = io.open("Userdata/ParityScreen.txt", "wb");
		file:write(string.format("screenshot at step %d of %s, camera pinned on %d %d\n", shotStep, SceneMan.Scene.PresetName, self.pin.X, self.pin.Y));
		file:close();
		print("PARITY SCREEN WRITTEN");
		if jit then
			os.exit(0);
		end
	end
end
