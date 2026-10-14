math.randomseed(os.time())

local state = {
    playerX = 0, playerY = 0,
    keyX = 0, keyY = 0,
    doorX = 0, doorY = 0,
    keyExists = true,
}

local GRID = 16

local function generate()
    local px, py, kx, ky, dx, dy
    repeat
        px = math.random(0, GRID - 1); py = math.random(0, GRID - 1)
        kx = math.random(0, GRID - 1); ky = math.random(0, GRID - 1)
        dx = math.random(0, GRID - 1); dy = math.random(0, GRID - 1)
    until not (px == kx and py == ky)
      and not (px == dx and py == dy)
      and not (kx == dx and ky == dy)

    state.playerX, state.playerY = px, py
    state.keyX, state.keyY = kx, ky
    state.doorX, state.doorY = dx, dy
    state.keyExists = true
end

function step(action)
    if action == "up" then
        local ny = state.playerY - 1
        if ny >= 0 then state.playerY = ny end
    elseif action == "down" then
        local ny = state.playerY + 1
        if ny < GRID then state.playerY = ny end
    elseif action == "left" then
        local nx = state.playerX - 1
        if nx >= 0 then state.playerX = nx end
    elseif action == "right" then
        local nx = state.playerX + 1
        if nx < GRID then state.playerX = nx end
    end

    if state.keyExists
       and state.playerX == state.keyX
       and state.playerY == state.keyY then
        state.keyExists = false
    end

    if not state.keyExists
       and state.playerX == state.doorX
       and state.playerY == state.doorY then
        generate()
    end
end

function getState()
    return state.playerX, state.playerY,
           state.keyX, state.keyY,
           state.doorX, state.doorY,
           state.keyExists
end

generate()