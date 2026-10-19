local executor, version = identifyexecutor()
print(string.format("[example] running on %s %s", executor, version))

local environment = getgenv()
if environment.example_started then
    print("[example] hello again")
else
    environment.example_started = true
    print("[example] first run on this Luau state")
end

local players = game:GetService("Players")
local list = players:GetPlayers()

print(string.format("[example] %d player(s) in this server", #list))
for _, player in list do
    print(string.format("[example] %s (@%s)", player.Name, player.DisplayName))
end
