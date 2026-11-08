-- scripts/StreamFlyover.lua
-- Flies the camera diagonally across the streaming test world (tools/make_stream_test_world.py)
-- so sectors stream in ahead of it and out behind it.

local speed = 40.0 -- metres per second

function OnUpdate(self, dt)
    self.position.x = self.position.x + speed * 0.7071 * dt
    self.position.z = self.position.z + speed * 0.7071 * dt
    if self.position.x > 500.0 then
        self.position.x = 20.0
        self.position.z = 20.0
    end
end
