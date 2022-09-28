# for the general idea of the game engine,
# I followed this tutorial:
# https://lodev.org/cgtutor/raycasting.html
# SCREEN RESOLUTION: 1280x720
from tkinter import *
import timeit
import math
import array
import os
import random
import time

paused = False
boss = False
stage = 1
hitting = False
screenwidth = 720
screenheight = 620
downscaleFactor = 10
roomsize = 0
mapArray = []
rooms = []
playerX, playerY = 1.5, 1.5  # initial position of the player
# initial position of the direction vector (where the player looks)
vectorX, vectorY = 1, 0
# initial position of the sideways movement vector (used for strafing)
movementX, movementY = 0, 1
# the camera plane, responsable for checking what can and can't be seen
cameraX, cameraY = 0, 0.66
textureW, textureH = 64, 64

# sprite variables
spriteDict = {'posX': [], 'posY': [], 'dist': [], 'monster': [], 'health': []}
spriteOrder = []

# external variables for smooth movement
vx, rx = 0, 0

# control variables
turnleft = 'a'
turnright = 'd'
gofwd = 'w'
goback = 's'
primaryAtk = 'k'

# character atributes
name = ""
health = 100
damage = 5
speed = 0
defence = 0
monsternum = 0
clearedRooms = 0
roomNumber = 0
score = 0
rang = 2

# display variables
screen = Tk()
screen.config(background="#6B3100", borderwidth=0, highlightthickness=0)
screen.title("Dragonslayr")
screen.geometry("1280x720")
bossimg = PhotoImage(file="boss.ppm")
nameVar = StringVar(screen, value="")
fwd = StringVar(screen, value=gofwd)
back = StringVar(screen, value=goback)
left = StringVar(screen, value=turnleft)
right = StringVar(screen, value=turnright)
atk = StringVar(screen, value=primaryAtk)
fact = IntVar(screen, value=downscaleFactor)
mainframe = Frame(screen, background="#6B3100", borderwidth=0,
                  highlightthickness=0)
leftframe = Frame(mainframe, background="#6B3100", borderwidth=0,
                  highlightthickness=0)
centerframe = Frame(mainframe, background="#6B3100", borderwidth=0,
                    highlightthickness=0)
rightframe = Frame(mainframe, background="#6B3100", borderwidth=0,
                   highlightthickness=0)
minimap = Canvas(leftframe, width=280, height=280, bg="#1B1B1B",
                 borderwidth=0, highlightthickness=0)
canvas = Canvas(centerframe, width=screenwidth, height=screenheight,
                bg="#1B1B1B", borderwidth=0, highlightthickness=0)
charphoto = Canvas(leftframe, width=280, height=440, background="#6B3100",
                   borderwidth=0, highlightthickness=0)
charbottom = Canvas(centerframe, width=720, height=100, background="#6B3100",
                    borderwidth=0, highlightthickness=0)
charright = Canvas(rightframe, width=280, height=720, background="#6B3100",
                   borderwidth=0, highlightthickness=0)
titleLabel = Label(screen, text="DRAGONSLAYR", width=100, font=("Arial", 25),
                   background="#1B1B1B", foreground="white")
newButton = Button(screen, text="New Game", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
loadButton = Button(screen, text="Load Game", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
optionsButton = Button(screen, text="Options", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
leaderButton = Button(screen, text="Leaderboard", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
exitButton = Button(screen, text="Exit", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
noButton = Button(screen, text="No", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
yesButton = Button(screen, text="Yes", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
exitLabel = Label(screen, text="Are you sure you want to quit?", width=100,
                  font=("Arial", 15), background="#1B1B1B", foreground="white")
optionsLabel = Label(screen,
                     text="Edit the entries to change the specified values",
                     width=100,
                     font=("Arial", 15), background="#1B1B1B",
                     foreground="white")
fwdEntry = Entry(screen, text="Move Forward", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
backEntry = Entry(screen, text="Move Back", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
leftEntry = Entry(screen, text="Turn Left", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
rightEntry = Entry(screen, text="Turn Right", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
atkEntry = Entry(screen, text="Attack", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
downEntry = Entry(screen, text="Downscale Factor", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
returnButton = Button(screen, text="Back", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
applyButton = Button(screen, text="Apply", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
saveButton = Button(screen, text="Save Game", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
resumeButton = Button(screen, text="Resume", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
bossButton = Button(screen, width=1280, height=720, image=bossimg,
                    activebackground="#1E1E1E", background="#1E1E1E")
levelUpLabel = Label(screen, width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
deathLabel = Label(screen, width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
speedButton = Button(screen, text="Character Speed", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
defenceButton = Button(screen, text="Character Defence", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
damageButton = Button(screen, text="Character Damage", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
nextLevelButton = Button(screen, text="Next Stage", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
newNameLabel = Label(screen, width=50,
                     text="Welcome, adventurer!\
                     \nHow do you want to be called?",
                     font=("Arial", 15), background="#1B1B1B",
                     foreground="white")
newNameButton = Button(screen, text="Continue", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
newNameEntry = Entry(screen, text="Name", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
slot1 = Button(screen, text="Save slot 1", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
slot2 = Button(screen, text="Save slot 2", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
slot3 = Button(screen, text="Save slot 3", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
slot4 = Button(screen, text="Save slot 4", width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white")
leaderLabel = Label(screen, width=50, font=(
    "Arial", 15), background="#1B1B1B", foreground="white", justify=LEFT,
    anchor=W)

# reads the movement variables and the downscale factor from the external file
with open("settings.txt") as file:
    gofwd = file.readline()[0]
    goback = file.readline()[0]
    turnleft = file.readline()[0]
    turnright = file.readline()[0]
    primaryAtk = file.readline()[0]
    downscaleFactor = int(file.readline())
    file.close()

# loads the wall texture
with open("wall.ppm") as file:
    textureArray = file.read().split()
    file.close()

# loads the floor texture
with open("floor.ppm") as file:
    floorArray = file.read().split()
    file.close()

# loads the celing texture
with open("ceiling.ppm") as file:
    ceilArray = file.read().split()
    file.close()

# loads the sprite textures
with open("pebble.ppm") as file:
    spriteArray = file.read().split()
    file.close()

# loads the monster textures
with open("monster.ppm") as file:
    monsterArray = file.read().split()
    file.close()

# loads the hand texture
with open("sword.ppm") as file:
    handArray = file.read().split()
    file.close()


# sorts the sprites by distance
def sortSprites():
    global spriteDict, spriteOrder
    for i in range(len(spriteDict['dist']) - 1):
        for j in range(i+1, len(spriteDict['dist'])):
            if(spriteDict['dist'][i] < spriteDict['dist'][j]):
                temp = spriteDict['dist'][i]
                spriteDict['dist'][i] = spriteDict['dist'][j]
                spriteDict['dist'][j] = temp

                temp = spriteDict['posX'][i]
                spriteDict['posX'][i] = spriteDict['posX'][j]
                spriteDict['posX'][j] = temp

                temp = spriteDict['posY'][i]
                spriteDict['posY'][i] = spriteDict['posY'][j]
                spriteDict['posY'][j] = temp

                temp = spriteDict['monster'][i]
                spriteDict['monster'][i] = spriteDict['monster'][j]
                spriteDict['monster'][j] = temp

                temp = spriteDict['health'][i]
                spriteDict['health'][i] = spriteDict['health'][j]
                spriteDict['health'][j] = temp

                temp = spriteOrder[i]
                spriteOrder[i] = spriteOrder[j]
                spriteOrder[j] = temp


# draws the game frame and minimap
def drawFrame():
    global mapArray, spriteOrder, hitting, monsternum, health, score, defence,\
           stage, rang
    colorArray = array.array('B', [0, 0, 0] * screenwidth * screenheight)
    windowHeight = int(screenheight / downscaleFactor)
    windowWidth = int(screenwidth / downscaleFactor)
    spriteBuffer = array.array('f', [0] * (windowWidth + 1))
    # renders the floor and ceiling
    for y in range(0, int(screenheight/2 + 1), downscaleFactor):
        # calculates the directions of the leftmost and rightmost ray on the
        # player's fov
        rayX0 = vectorX - cameraX
        rayX1 = vectorX + cameraX
        rayY0 = vectorY - cameraY
        rayY1 = vectorY + cameraY

        screenpos = int(y/downscaleFactor - windowHeight/2)

        cameraHeight = windowHeight/2
        floorDist = windowHeight
        if(screenpos != 0):
            floorDist = cameraHeight / screenpos

        # How much to increase the floor texture related to the screen
        floorStepX = floorDist * (rayX1 - rayX0) / windowWidth
        floorStepY = floorDist * (rayY1 - rayY0) / windowWidth

        # coordinates for the column on the left
        floorX = playerX + floorDist * rayX0
        floorY = playerY + floorDist * rayY0

        for x in range(0, screenwidth, downscaleFactor):
            # coordinates of the map square
            floorMapX = int(floorX)
            floorMapY = int(floorY)
            # coordinates of the texture to be displayed
            textureX = int(textureW * (floorX - floorMapX)) & (textureW-1)
            textureY = int(textureH * (floorY - floorMapY)) & (textureH-1)

            floorX += floorStepX
            floorY += floorStepY

            # fills the ceiling
            colorR = int(
                ceilArray[textureY * textureW * 3 + 3 * textureX + 4])
            colorG = int(
                ceilArray[textureY * textureW * 3 + 3 * textureX + 5])
            colorB = int(
                ceilArray[textureY * textureW * 3 + 3 * textureX + 6])

            if(stage % 2 == 1):
                colorArray[y * screenwidth * 3 + x * 3] = colorR
                colorArray[y * screenwidth * 3 + x * 3 + 1] = colorG
                colorArray[y * screenwidth * 3 + x * 3 + 2] = colorB
            else:
                colorArray[y * screenwidth * 3 + x * 3] = colorB
                colorArray[y * screenwidth * 3 + x * 3 + 1] = colorG
                colorArray[y * screenwidth * 3 + x * 3 + 2] = colorR

            # fills the floor
            colorR = int(
                floorArray[textureY * textureW * 3 + 3 * textureX + 4])
            colorG = int(
                floorArray[textureY * textureW * 3 + 3 * textureX + 5])
            colorB = int(
                floorArray[textureY * textureW * 3 + 3 * textureX + 6])

            colorArray[(screenheight - y - downscaleFactor -
                        screenheight % downscaleFactor) *
                       screenwidth * 3 + x * 3] = colorR
            colorArray[(screenheight - y - downscaleFactor -
                        screenheight % downscaleFactor) *
                       screenwidth * 3 + x * 3 + 1] = colorG
            colorArray[(screenheight - y - downscaleFactor -
                        screenheight % downscaleFactor) *
                       screenwidth * 3 + x * 3 + 2] = colorB

    # renders the walls
    for i in range(0, screenwidth, downscaleFactor):
        screenI = 2 * i / screenwidth - 1  # the x coordinate on the screen
        # calculate the direction of the ray corresponding to the coordinate
        rayX = vectorX + cameraX * screenI
        rayY = vectorY + cameraY * screenI
        # checks if the ray collides with a wall using a DDA algorith
        # (checking every 'edge' of a map square)
        mapX, mapY = int(playerX), int(playerY)
        edgeX, edgeY = 0, 0
        deltaX, deltaY = 0, 0
        wallDistance = ""
        # overcomes the division by 0 cases
        if(rayX == 0):
            deltaX, deltaY = 1, 0
        elif(rayY == 0):
            deltaX, deltaY = 0, 1
        else:
            deltaX, deltaY = abs(1/rayX), abs(1/rayY)

        # what square should the ray travel next
        nextX, nextY = 0, 0

        wallHit = False  # does the ray hit a wall
        side = 0  # what side of the wall does the ray hit

        # calculate the steps and initial distance to the edges
        if(rayX < 0):
            nextX = -1
            edgeX = (playerX - mapX) * deltaX
        else:
            nextX = 1
            edgeX = (mapX + 1 - playerX) * deltaX
        if(rayY < 0):
            nextY = -1
            edgeY = (playerY - mapY) * deltaY
        else:
            nextY = 1
            edgeY = (mapY + 1 - playerY) * deltaY

        # actual DDA algorithm starts here
        while wallHit is False:
            # checks the next map square, either in the x or y direction
            if(edgeX < edgeY):
                edgeX += deltaX
                mapX += nextX
                side = 0
            else:
                edgeY += deltaY
                mapY += nextY
                side = 1
            if(mapArray[mapX][mapY] == 1):
                wallHit = True

        # get the distance of the ray projected on the camera plane in order
        # to avoid creating a fisheye effect

        if(side == 0):
            wallDistance = (mapX - playerX + (1 - nextX) / 2) / rayX
        else:
            wallDistance = (mapY - playerY + (1 - nextY) / 2) / rayY
        # how tall will the line be on the screen
        lineHeight = 0
        if(wallDistance == 0):
            lineHeight == windowHeight
        else:
            lineHeight = int(windowHeight / wallDistance)

        # get the coordinates of the line
        lineStart = -lineHeight / 2 + windowHeight / 2
        if(lineStart < 0):
            lineStart = 0
        lineEnd = lineHeight / 2 + windowHeight / 2
        if(lineEnd >= windowHeight):
            lineEnd = windowHeight - 1

        # calculates the exact point where the wall was hit
        wallPosition = playerX + wallDistance * rayX
        if(side == 0):
            wallPosition = playerY + wallDistance * rayY
        wallPosition -= math.floor(wallPosition)

        # calculates the X coordinate of the texture
        textureX = int(wallPosition * textureW)
        if(side == 1 and rayY < 0):
            textureX = textureW - textureX - 1
        if(side == 0 and rayX > 0):
            textureX = textureW - textureX - 1

        # how much to increase the texture related to the screen
        if(lineHeight == 0):
            continue
        textureStep = textureH / lineHeight
        texturePosition = (lineStart - windowHeight / 2 +
                           lineHeight / 2) * textureStep
        for y in range(int(lineStart) * downscaleFactor, int(lineEnd) *
                       downscaleFactor + 1, downscaleFactor):
            # calculates the texture coordinates
            textureY = int(texturePosition) & (textureH - 1)
            texturePosition += textureStep
            colorR = int(
                textureArray[textureY * textureW * 3 + 3 * textureX + 4])
            colorG = int(
                textureArray[textureY * textureW * 3 + 3 * textureX + 5])
            colorB = int(
                textureArray[textureY * textureW * 3 + 3 * textureX + 6])
            # makes the colors darker if the side is 1, so it can create a
            # shadow effect
            if(side == 1):
                colorR -= 10
                colorG -= 10
                colorB -= 10
            if(stage % 2 == 1):
                colorArray[y * screenwidth * 3 + i * 3] = colorR
                colorArray[y * screenwidth * 3 + i * 3 + 1] = colorG
                colorArray[y * screenwidth * 3 + i * 3 + 2] = colorB
            else:
                colorArray[y * screenwidth * 3 + i * 3] = colorB
                colorArray[y * screenwidth * 3 + i * 3 + 1] = colorG
                colorArray[y * screenwidth * 3 + i * 3 + 2] = colorR
        spriteBuffer[int(i/downscaleFactor)] = wallDistance

    # renders the sprites
    spriteOrder = []
    spriteDict['dist'] = []
    # calculates the distance of each sprite to the player
    for i in range(len(spriteDict['posX'])):
        spriteOrder.append(i)
        spriteDict['dist'].append((playerX - spriteDict['posX'][i]) *
                                  (playerX - spriteDict['posX'][i]) +
                                  (playerY - spriteDict['posY'][i]) *
                                  (playerY - spriteDict['posY'][i]))
    sortSprites()

    # projects the sprites and draws them
    for i in range(len(spriteDict['posX'])):
        # the sprite position on the screen
        spriteCameraX = spriteDict['posX'][spriteOrder[i]] - playerX
        spriteCameraY = spriteDict['posY'][spriteOrder[i]] - playerY

        # used to calculate the inverse camera matrix
        delta = 1 / (cameraX * vectorY - vectorX * cameraY)

        newX = delta * (vectorY * spriteCameraX - vectorX * spriteCameraY)
        newY = delta * (cameraX * spriteCameraY - cameraY * spriteCameraX)

        # added so that the game doesn't crash if the sprite is on the same
        # position as the player
        if(newY == 0):
            continue

        spriteWindowX = int(((windowWidth) / 2) * (1 + newX / newY))

        # calculates the height of the sprite
        spriteHeight = abs(int((windowHeight) / newY))

        # calculates the lowest and highest pixel of the sprite, similar to the
        # walls
        spriteStartY = (windowHeight) / 2 - spriteHeight / 2
        if(spriteStartY < 0):
            spriteStartY = 0
        spriteEndY = (windowHeight) / 2 + spriteHeight / 2
        if(spriteEndY > windowHeight):
            spriteEndY = windowHeight

        # calculates leftmost and rightmost points of the sprites

        # does this because the sprite image is a square
        spriteWidth = abs(int((windowHeight) / newY))

        spriteStartX = spriteWindowX - spriteWidth / 2
        if(spriteStartX < 0):
            spriteStartX = 0
        spriteEndX = spriteWindowX + spriteWidth / 2
        if(spriteEndX > windowWidth):
            spriteEndX = windowWidth

        for x in range(int(spriteStartX), int(spriteEndX)):
            # calculates the texture coordinates
            spriteX = int((x + spriteWidth/2 - spriteWindowX) *
                          textureW / (spriteWidth))

            # these conditions are there so that only the visible sprites are
            # drawn
            if(newY > 0 and x > 0 and x < windowWidth and
               newY < spriteBuffer[x] and spriteX >= 0):
                for y in range(int(spriteStartY),
                               int(spriteEndY)):
                    temp = y - windowHeight / 2 + spriteHeight / 2
                    spriteY = int((temp * textureH) / spriteHeight)
                    if(spriteY >= 0):
                        if(spriteDict['monster'][i] is False):
                            colorR = int(spriteArray[spriteY * textureW * 3 +
                                                     spriteX * 3 + 4])
                            colorG = int(spriteArray[spriteY * textureW * 3 +
                                                     spriteX * 3 + 5])
                            colorB = int(spriteArray[spriteY * textureW * 3 +
                                                     spriteX * 3 + 6])
                        else:
                            colorR = int(monsterArray[spriteY * textureW * 3 +
                                                      spriteX * 3 + 4])
                            colorG = int(monsterArray[spriteY * textureW * 3 +
                                                      spriteX * 3 + 5])
                            colorB = int(monsterArray[spriteY * textureW * 3 +
                                                      spriteX * 3 + 6])
                        if(colorR != 0 or colorG != 0 or colorB != 0):
                            if(stage % 2 == 1):
                                colorArray[y * screenwidth * downscaleFactor *
                                           3 + x * downscaleFactor * 3] =\
                                    colorR
                                colorArray[y * screenwidth * downscaleFactor *
                                           3 + x * downscaleFactor * 3 + 1] =\
                                    colorG
                                colorArray[y * screenwidth * downscaleFactor *
                                           3 + x * downscaleFactor * 3 + 2] =\
                                    colorB
                            else:
                                colorArray[y * screenwidth * downscaleFactor *
                                           3 + x * downscaleFactor * 3] =\
                                    colorB
                                colorArray[y * screenwidth * downscaleFactor *
                                           3 + x * downscaleFactor * 3 + 1] =\
                                    colorG
                                colorArray[y * screenwidth * downscaleFactor *
                                           3 + x * downscaleFactor * 3 + 2] =\
                                    colorR

    # renders the hand on the screen
    if(hitting is False):
        handPosX = screenwidth - screenwidth % downscaleFactor - \
            4 * textureW + (4 * textureW) % downscaleFactor
        handPosY = screenheight - screenheight % downscaleFactor - \
            4 * textureH + (4 * textureH) % downscaleFactor

        for x in range((textureW * 4) % downscaleFactor,
                       textureW * 4, downscaleFactor):
            for y in range((textureH * 4) % downscaleFactor,
                           textureH * 4, downscaleFactor):
                # ensures that only the coloured pixels get drawn, and not the
                # whole sprite
                if(int(handArray[int(y / 4) * textureW * 3 +
                                 int(x / 4) * 3 + 4]) != 0 and
                   int(handArray[int(y / 4) * textureW * 3 +
                                 int(x / 4) * 3 + 5]) != 0 and
                   int(handArray[int(y / 4) * textureW * 3 +
                                 int(x / 4) * 3 + 6]) != 0):
                    colorArray[handPosY *
                               screenwidth * 3 +
                               handPosX * 3] =\
                        int(handArray[int(y / 4) * textureW * 3 +
                                      int(x / 4) * 3 + 4])
                    colorArray[handPosY *
                               screenwidth * 3 +
                               handPosX * 3 + 1] =\
                        int(handArray[int(y / 4) * textureW * 3 +
                                      int(x / 4) * 3 + 5])
                    colorArray[handPosY *
                               screenwidth * 3 +
                               handPosX * 3 + 2] =\
                        int(handArray[int(y / 4) * textureW * 3 +
                                      int(x / 4) * 3 + 6])
                handPosY += downscaleFactor
            handPosY = screenheight - screenheight % downscaleFactor - \
                4 * textureH + (4 * textureH) % downscaleFactor
            handPosX += downscaleFactor
    else:
        handPosX = screenwidth - screenwidth % downscaleFactor - \
            6 * textureW + (6 * textureW) % downscaleFactor
        handPosY = screenheight - screenheight % downscaleFactor - \
            6 * textureH + (6 * textureH) % downscaleFactor

        for x in range((textureW * 6) % downscaleFactor,
                       textureW * 6, downscaleFactor):
            for y in range((textureH * 6) % downscaleFactor,
                           textureH * 6, downscaleFactor):
                # ensures that only the coloured pixels get drawn, and not the
                # whole sprite
                if(int(handArray[int(y / 6) * textureW * 3 +
                                 int(x / 6) * 3 + 4]) != 0 and
                   int(handArray[int(y / 6) * textureW * 3 +
                                 int(x / 6) * 3 + 5]) != 0 and
                   int(handArray[int(y / 6) * textureW * 3 +
                                 int(x / 6) * 3 + 6]) != 0):
                    colorArray[handPosY *
                               screenwidth * 3 +
                               handPosX * 3] =\
                        int(handArray[int(y / 6) * textureW * 3 +
                                      int(x / 6) * 3 + 4])
                    colorArray[handPosY *
                               screenwidth * 3 +
                               handPosX * 3 + 1] =\
                        int(handArray[int(y / 6) * textureW * 3 +
                                      int(x / 6) * 3 + 5])
                    colorArray[handPosY *
                               screenwidth * 3 +
                               handPosX * 3 + 2] =\
                        int(handArray[int(y / 6) * textureW * 3 +
                                      int(x / 6) * 3 + 6])
                handPosY += downscaleFactor
            handPosY = screenheight - screenheight % downscaleFactor - \
                6 * textureH + (6 * textureH) % downscaleFactor
            handPosX += downscaleFactor

    # draws the frame on the canvas from the secodary array
    for x in range(0, screenwidth, downscaleFactor):
        for y in range(0, screenheight, downscaleFactor):
            colorR = hex(colorArray[y * screenwidth *
                                    3 + x * 3]).rsplit('x')[1]
            colorG = hex(colorArray[y * screenwidth *
                                    3 + x * 3 + 1]).rsplit('x')[1]
            colorB = hex(colorArray[y * screenwidth *
                                    3 + x * 3 + 2]).rsplit('x')[1]

            if(len(colorR) < 2):
                colorR = '0' + colorR
            if(len(colorG) < 2):
                colorG = '0' + colorG
            if(len(colorB) < 2):
                colorB = '0' + colorB

            canvas.create_rectangle(x, y, x + downscaleFactor / 2,
                                    y + downscaleFactor / 2,
                                    outline="#" + colorR +
                                    colorG + colorB, fill="#" + colorR +
                                    colorG + colorB)

    # draws a crosshair on the main screen
    if(hitting is False):
        canvas.create_oval(screenwidth / 2 - 4, screenheight / 2 - 4,
                           screenwidth / 2 + 4, screenheight / 2 + 4,
                           outline="white")
    else:
        canvas.create_oval(screenwidth / 2 - 4, screenheight / 2 - 4,
                           screenwidth / 2 + 4, screenheight / 2 + 4,
                           outline="red")

    # draws the minimap
    minimap.create_rectangle(0, 0, 280, 280, outline="#6B3100",
                             fill="#6B3100")
    minimapStep = int(280/len(mapArray[1]))
    room = int(playerX / len(mapArray[0]))
    for y in range(0, 280-minimapStep, minimapStep):
        for x in range(0, 280-minimapStep, minimapStep):
            if(rooms[room][int(x/minimapStep)][int(y/minimapStep)] != 1):
                minimap.create_rectangle(x, y, x+minimapStep, y+minimapStep,
                                         fill="gray", outline="gray")
            else:
                minimap.create_rectangle(x, y, x+minimapStep, y+minimapStep,
                                         fill="brown", outline="brown")
    # draws the player's position on the minimap
    minimap.create_oval((playerX * minimapStep - 4) -
                        room * len(mapArray[0]) * minimapStep,
                        playerY * minimapStep - 4,
                        (playerX * minimapStep + 4) -
                        room * len(mapArray[0]) * minimapStep,
                        playerY * minimapStep + 4,
                        fill="red", outline="red")
    minimap.create_line(playerX * minimapStep -
                        room * len(mapArray[0]) * minimapStep,
                        playerY * minimapStep,
                        (playerX + vectorX) * minimapStep -
                        room * len(mapArray[0]) * minimapStep,
                        (playerY+vectorY) *
                        minimapStep, fill="red")
    # draws the rocks' and monsters' positions on the map
    for i in range(len(spriteDict['posX'])):
        if(spriteDict['monster'][i] is True):
            minimap.create_oval(spriteDict['posX'][i] * minimapStep - 4 -
                                room * len(mapArray[0]) * minimapStep,
                                spriteDict['posY'][i] * minimapStep - 4,
                                spriteDict['posX'][i] * minimapStep + 4 -
                                room * len(mapArray[0]) * minimapStep,
                                spriteDict['posY'][i] * minimapStep + 4,
                                fill="brown", outline="brown")
        else:
            minimap.create_oval(spriteDict['posX'][i] * minimapStep - 4 -
                                room * len(mapArray[0]) * minimapStep,
                                spriteDict['posY'][i] * minimapStep - 4,
                                spriteDict['posX'][i] * minimapStep + 4 -
                                room * len(mapArray[0]) * minimapStep,
                                spriteDict['posY'][i] * minimapStep + 4,
                                fill="darkgray", outline="darkgray")
    # draws the health bar
    charbottom.create_text(360, 15, font="Arial",
                           text="Health: " + str(health),
                           fill="white")
    charbottom.create_rectangle(19, 39, 701, 81, outline="black", fill="black")
    charbottom.create_rectangle(20, 40, int(680 * health / 100) + 20, 80,
                                outline="red", fill="red")

    charphoto.create_text(140, 50, font="Arial",
                          text="SCORE: " + str(score), fill="white")
    charphoto.create_text(140, 70, font="Arial",
                          text="DEFENCE: " + str(defence), fill="white")
    charphoto.create_text(140, 90, font="Arial",
                          text="DAMAGE: " + str(damage), fill="white")
    charphoto.create_text(140, 110, font="Arial",
                          text="SPEED: " + str(speed), fill="white")

    # handles the damage system
    for i in range(len(spriteDict['posX'])):
        if(spriteDict['monster'][i] is True):
            # the player has a slightly larger range than the monsters
            if(spriteDict['dist'][i] < rang):
                if(hitting is True):
                    spriteDict['health'][i] -= damage
                    hitting = False
            # if the monster is close enough, then it will hit the player
            if(spriteDict['dist'][i] < 1):
                # if the monster is dead, then it turns into a rock
                if(spriteDict['health'][i] <= 0):
                    monsternum -= 1
                    score += 10
                    spriteDict['monster'][i] = False
                    continue
                p = random.randint(1, 100)
                if(p % 25 == 0):
                    if(4 - defence / 2 + stage > 0):
                        health -= (4 - defence / 2 + stage)
    if(hitting is True):
        hitting = False
    charphoto.update()
    charbottom.update()
    minimap.update()
    canvas.update()


def specup(stat):
    global defence, speed, damage, clearedRooms
    if(stat == 0):
        defence += 1
    elif(stat == 1):
        speed += 1
    else:
        damage += 1
    clearedRooms -= 1
    nextLevel()


# renders the next level dialogue
def nextLevel():
    global stage, roomNumber, clearedRooms
    clearScreen()
    titleLabel.pack(pady=50)
    levelUpLabel.config(text="Well done! You finished this stage and gained " +
                        str(roomNumber) + " levels\n\nLevel up Points left: " +
                        str(clearedRooms))
    levelUpLabel.pack(pady=20)
    if(clearedRooms > 0):
        defenceButton.config(command=lambda: specup(0), state=ACTIVE)
        speedButton.config(command=lambda: specup(1), state=ACTIVE)
        damageButton.config(command=lambda: specup(2), state=ACTIVE)
        nextLevelButton.config(state=DISABLED)
    else:
        defenceButton.config(state=DISABLED)
        speedButton.config(state=DISABLED)
        damageButton.config(state=DISABLED)
        nextLevelButton.config(command=initGameScreen, state=ACTIVE)
    defenceButton.pack(pady=20)
    speedButton.pack(pady=20)
    damageButton.pack(pady=20)
    nextLevelButton.pack(pady=20)


# main game loop
def gameloop():
    global playerX, playerY, vectorX, vectorY, cameraX, cameraY, paused, boss,\
        monsternum, clearedRooms, health, score, hitting, stage

    while paused is False and boss is False:
        if(health <= 0):
            paused = True
            updateLeaderboard()
            deathScreen()
        if(monsternum == 0):
            if(clearedRooms > 0):
                mapArray[len(mapArray[0]) *
                         clearedRooms][int(len(mapArray[0]) / 2)] = 0
            if(clearedRooms < roomNumber):
                mapArray[len(mapArray[0]) * (clearedRooms + 1) -
                         1][int(len(mapArray[0]) / 2)] = 0
            score += 100
            clearedRooms += 1
            monsternum = -1
        if(playerX > clearedRooms * len(mapArray[0]) and
           int(playerX) % len(mapArray[0]) > 1 and monsternum == -1):
            spawnMonsters(clearedRooms)
        if(clearedRooms == roomNumber):
            canvas.create_text(360, 100, font=("Arial", 20),
                               text="Stage Complete!", fill="white")
            canvas.update()
            score += 250
            time.sleep(2)
            paused = True
            stage += 1
            nextLevel()
        start = timeit.default_timer()
        canvas.delete("all")
        minimap.delete("all")
        charright.delete("all")
        charbottom.delete("all")
        charphoto.delete("all")
        drawFrame()
        stop = timeit.default_timer()
        frame = stop - start
        charright.create_text(140, 20, font="Arial",
                              text=str(round(1 / frame, 2)), fill="white")
        charright.update()
        playerSpeed = frame * 2.5
        rotationSpeed = frame * 2

        # moves all of the monsters towards the player
        for i in range(len(spriteDict['posX'])):
            if(spriteDict['monster'][i] is True):
                p = random.randint(1, 100)
                if(p % 5 == 0):
                    # moves the monster
                    if(mapArray[int(spriteDict['posX'][i] -
                       (spriteDict['posX'][i] - playerX) /
                       (len(mapArray[0]) - 2))][int(spriteDict['posY'][i])] !=
                       1):
                        spriteDict['posX'][i] -= (spriteDict['posX'][i] -
                                                  playerX) /\
                                                 (len(mapArray[0]) - 2)
                    if(mapArray[int(spriteDict['posX'][i])]
                               [int(spriteDict['posY'][i] -
                                    (spriteDict['posY'][i] - playerY) /
                                    (len(mapArray[0]) - 2))] != 1):
                        spriteDict['posY'][i] -= (spriteDict['posY'][i] -
                                                  playerY) /\
                                                  (len(mapArray[0]) - 2)

        # the added ifs are for collision detection
        # for forward and backward movement
        if(mapArray[int(playerX + vectorX * playerSpeed * vx)]
           [int(playerY)] != 1):
            playerX += vectorX * playerSpeed * vx
        if(mapArray[int(playerX)]
           [int(playerY + vectorY * playerSpeed * vx)] != 1):
            playerY += vectorY * playerSpeed * vx

        # rotates the direction vector, movement vector and the camera plane of
        # the player by multiplying it with the rotation matrix
        temp = vectorX
        vectorX = vectorX * math.cos(rotationSpeed * rx) - \
            vectorY * math.sin(rotationSpeed * rx)
        vectorY = temp * math.sin(rotationSpeed * rx) + \
            vectorY * math.cos(rotationSpeed * rx)
        temp = cameraX
        cameraX = cameraX * math.cos(rotationSpeed * rx) - \
            cameraY * math.sin(rotationSpeed * rx)
        cameraY = temp * math.sin(rotationSpeed * rx) + \
            cameraY * math.cos(rotationSpeed * rx)


# displays the load game screen
def loadGame(state):
    clearScreen()
    titleLabel.pack(pady=50)
    # checks every save file, if it's empty then it grays out the button
    with open("1.sav") as file:
        temp = file.read()
        if(temp != ''):
            slot1.config(command=lambda: load(1), state=ACTIVE)
        else:
            slot1.config(state=DISABLED)
    slot1.pack(pady=20)
    with open("2.sav") as file:
        temp = file.read()
        if(temp != ''):
            slot2.config(command=lambda: load(2), state=ACTIVE)
        else:
            slot2.config(state=DISABLED)
    slot2.pack(pady=20)
    with open("3.sav") as file:
        temp = file.read()
        if(temp != ''):
            slot3.config(command=lambda: load(3), state=ACTIVE)
        else:
            slot3.config(state=DISABLED)
    slot3.pack(pady=20)
    with open("4.sav") as file:
        temp = file.read()
        if(temp != ''):
            slot4.config(command=lambda: load(4), state=ACTIVE)
        else:
            slot4.config(state=DISABLED)
    slot4.pack(pady=20)
    if(state == 0):
        returnButton.config(command=drawMenu)
    else:
        returnButton.config(command=pauseMenu)
    returnButton.pack(pady=20)


# loads the game
def load(num):
    global name, spriteDict, playerX, playerY, cameraX, cameraY,\
           vectorX, vectorY, damage, defence, speed, mapArray, stage,\
           clearedRooms, monsternum, score, health, roomNumber, roomsize,\
           rooms, spriteOrder, paused, mapArray
    with open(str(num) + ".sav") as file:
        spriteOrder = []
        paused = False
        mapArray = []
        rooms = []
        spriteDict = {'posX': [], 'posY': [], 'dist': [], 'monster': [],
                      'health': []}
        name = file.readline()
        playerX = float(file.readline())
        playerY = float(file.readline())
        cameraX = float(file.readline())
        cameraY = float(file.readline())
        vectorX = float(file.readline())
        vectorY = float(file.readline())
        damage = int(file.readline())
        defence = int(file.readline())
        speed = int(file.readline())
        stage = int(file.readline())
        clearedRooms = int(file.readline())
        monsternum = int(file.readline())
        score = int(file.readline())
        health = float(file.readline())
        roomNumber = int(file.readline())
        roomsize = int(file.readline())
        temp = file.readline().split()
        for i in range(len(temp)):
            spriteDict['posX'].append(float(temp[i]))
        temp = file.readline().split()
        for i in range(len(temp)):
            spriteDict['posY'].append(float(temp[i]))
        temp = file.readline().split()
        for i in range(len(temp)):
            if(temp[i] == 'True'):
                spriteDict['monster'].append(True)
            else:
                spriteDict['monster'].append(False)
        temp = file.readline().split()
        for i in range(len(temp)):
            spriteDict['health'].append(float(temp[i]))
        for i in range(roomNumber):
            temparray = []
            for j in range(roomsize):
                temp = file.readline().split()
                for k in range(len(temp)):
                    temp[k] = int(temp[k])
                temparray.append(temp)
                mapArray.append(temp)
            rooms.insert(i, temparray)
        file.close()
    resumeGame()


# saves the game
def save(num):
    global name, spriteDict, playerX, playerY, cameraX, cameraY,\
           vectorX, vectorY, damage, defence, speed, rooms, stage,\
           clearedRooms, monsternum, score, health, roomNumber, roomsize
    with open(str(num) + ".sav", "w") as file:
        file.write(name + '\n' +
                   str(playerX) + '\n' +
                   str(playerY) + '\n' +
                   str(cameraX) + '\n' +
                   str(cameraY) + '\n' +
                   str(vectorX) + '\n' +
                   str(vectorY) + '\n' +
                   str(damage) + '\n' +
                   str(defence) + '\n' +
                   str(speed) + '\n' +
                   str(stage) + '\n' +
                   str(clearedRooms) + '\n' +
                   str(monsternum) + '\n' +
                   str(score) + '\n' +
                   str(health) + '\n' +
                   str(roomNumber) + '\n' +
                   str(roomsize) + '\n')
        for i in range(len(spriteDict['posX'])):
            file.write(str(spriteDict['posX'][i]) + ' ')
        file.write('\n')
        for i in range(len(spriteDict['posX'])):
            file.write(str(spriteDict['posY'][i]) + ' ')
        file.write('\n')
        for i in range(len(spriteDict['posX'])):
            file.write(str(spriteDict['monster'][i]) + ' ')
        file.write('\n')
        for i in range(len(spriteDict['posX'])):
            file.write(str(spriteDict['health'][i]) + ' ')
        file.write('\n')
        for i in range(roomNumber):
            for j in range(len(rooms[0])):
                for k in range(len(rooms[0])):
                    file.write(str(rooms[i][j][k]) + ' ')
                file.write('\n')
        file.close()
    resumeGame()


# displays the save screen
def saveGame():
    clearScreen()
    titleLabel.pack(pady=50)
    slot1.config(command=lambda: save(1))
    slot1.pack(pady=20)
    slot2.config(command=lambda: save(2))
    slot2.pack(pady=20)
    slot3.config(command=lambda: save(3))
    slot3.pack(pady=20)
    slot4.config(command=lambda: save(4))
    slot4.pack(pady=20)
    returnButton.config(command=pauseMenu)
    returnButton.pack(pady=20)


# applies the settings
def applySettings():
    global gofwd, goback, turnleft, turnright, primaryAtk, downscaleFactor,\
           defence, damage, rang, score
    # using this so that the user doesn't write in the entries after the game
    # was resumed
    applyButton.focus_set()
    # CHEAT CODE SECTION (and the reason why the entries aren't labeled)
    if(fwd.get() == "Power Overwhelming"):
        damage = 1000
        fwd.set(gofwd)
    elif(fwd.get() == "Kunami Code"):
        defence = 1000
        fwd.set(gofwd)
    elif(fwd.get() == "Gamer Moment"):
        score += 1000
        fwd.set(gofwd)
    elif(len(fwd.get()) == 1):
        gofwd = fwd.get()
    if(len(back.get()) == 1):
        goback = back.get()
    if(len(left.get()) == 1):
        turnleft = left.get()
    if(len(right.get()) == 1):
        turnright = right.get()
    if(len(right.get()) == 1):
        primaryAtk = atk.get()
    try:
        downscaleFactor = fact.get()
    except:
        applyButton.focus_set()
    screen.bind("<KeyRelease-" + gofwd.lower() + ">", stop_playerMovement)
    screen.bind("<KeyRelease-" + goback.lower() + ">", stop_playerMovement)
    screen.bind("<KeyRelease-" + turnleft.lower() + ">", stop_playerRotation)
    screen.bind("<KeyRelease-" + turnright.lower() + ">", stop_playerRotation)


# displays the options screen
def options(state):
    clearScreen()
    titleLabel.pack(pady=50)
    optionsLabel.pack(pady=20)
    fwdEntry.config(textvariable=fwd)
    fwdEntry.pack(pady=10)
    backEntry.config(textvariable=back)
    backEntry.pack(pady=10)
    leftEntry.config(textvariable=left)
    leftEntry.pack(pady=10)
    rightEntry.config(textvariable=right)
    rightEntry.pack(pady=10)
    atkEntry.config(textvariable=atk)
    atkEntry.pack(pady=10)
    downEntry.config(textvariable=fact)
    downEntry.pack(pady=10)
    applyButton.config(command=applySettings)
    applyButton.pack(pady=20)
    if(state == 0):
        returnButton.config(command=drawMenu)
    else:
        returnButton.config(command=pauseMenu)
    returnButton.pack(pady=20)


# displays the leaderboard screen
def leaderboard(state):
    clearScreen()
    titleLabel.pack(pady=50)
    with open("leaderboard.txt") as file:
        leaderLabel.config(text=file.read())
        file.close()
    leaderLabel.pack(pady=20)
    if(state == 0):
        returnButton.config(command=drawMenu)
    else:
        returnButton.config(command=deathScreen)
    returnButton.pack(pady=20)


# updates the leaderboard external file
def updateLeaderboard():
    global score
    temp = []
    with open("leaderboard.txt") as file:
        for i in range(10):
            temp.append(file.readline().split())
        file.close()
    for i in range(10):
        if(score > int(temp[i][len(temp[i]) - 1])):
            tempLine = [str(i + 1), name, str(score)]
            temp.insert(i, tempLine)
            break
    with open("leaderboard.txt", "w") as file:
        for i in range(10):
            for j in range(len(temp[i])):
                file.write(temp[i][j] + ' ')
            file.write('\n')
        file.close()


# displays the game over screen
def deathScreen():
    global score
    clearScreen()
    titleLabel.pack(pady=50)
    deathLabel.config(text="You Died!\nScore: " + str(score))
    deathLabel.pack(pady=100)
    leaderButton.config(command=lambda: leaderboard(1))
    leaderButton.pack(pady=20)
    exitButton.config(command=quit)
    exitButton.pack(pady=50)


# clears the screen by removing all of the widgets
def clearScreen():
    mainframe.pack_forget()
    leftframe.pack_forget()
    rightframe.pack_forget()
    centerframe.pack_forget()
    minimap.pack_forget()
    canvas.pack_forget()
    charphoto.pack_forget()
    charbottom.pack_forget()
    charright.pack_forget()
    titleLabel.pack_forget()
    newButton.pack_forget()
    loadButton.pack_forget()
    optionsButton.pack_forget()
    leaderButton.pack_forget()
    exitButton.pack_forget()
    yesButton.pack_forget()
    noButton.pack_forget()
    exitLabel.pack_forget()
    optionsLabel.pack_forget()
    fwdEntry.pack_forget()
    backEntry.pack_forget()
    leftEntry.pack_forget()
    rightEntry.pack_forget()
    downEntry.pack_forget()
    atkEntry.pack_forget()
    returnButton.pack_forget()
    applyButton.pack_forget()
    saveButton.pack_forget()
    resumeButton.pack_forget()
    bossButton.pack_forget()
    levelUpLabel.pack_forget()
    deathLabel.pack_forget()
    speedButton.pack_forget()
    defenceButton.pack_forget()
    damageButton.pack_forget()
    nextLevelButton.pack_forget()
    newNameButton.pack_forget()
    newNameEntry.pack_forget()
    newNameLabel.pack_forget()
    slot1.pack_forget()
    slot2.pack_forget()
    slot3.pack_forget()
    slot4.pack_forget()
    leaderLabel.pack_forget()


# quits the game
def quit():
    # updates the external settings file before exiting
    with open("settings.txt", 'w') as file:
        file.write(gofwd + '\n' + goback + '\n' + turnleft + '\n' +
                   turnright + '\n' + primaryAtk + '\n' + str(downscaleFactor))
        file.close()
    os._exit(0)


# displays the exit dialogue
def exitDialogue(state):
    clearScreen()
    titleLabel.pack(pady=50)
    exitLabel.pack(pady=20)
    yesButton.config(command=quit)
    yesButton.pack(pady=20)
    if(state == 0):
        noButton.config(command=drawMenu)
    else:
        noButton.config(command=pauseMenu)
    noButton.pack(pady=20)


# changes the screen to the boss image for the boss key
def bossKey():
    clearScreen()
    bossButton.config(image=bossimg, compound=LEFT)
    if(len(mapArray) > 0):
        bossButton.config(command=resumeGame)
    else:
        bossButton.config(command=drawMenu)
    bossButton.pack(side=TOP)


# resumes the game
def resumeGame():
    global paused, boss
    paused = False
    boss = False
    clearScreen()
    mainframe.pack(expand=TRUE)
    leftframe.pack(side=LEFT)
    centerframe.pack(side=LEFT)
    rightframe.pack(side=LEFT)
    minimap.pack(side=TOP)
    charphoto.pack(side=TOP)
    canvas.pack(side=TOP)
    charbottom.pack(side=TOP)
    charright.pack()
    gameloop()


# displays the pause menu
def pauseMenu():
    clearScreen()
    titleLabel.pack(pady=50)
    resumeButton.config(command=resumeGame)
    resumeButton.pack(pady=20)
    saveButton.config(command=saveGame)
    saveButton.pack(pady=20)
    loadButton.config(command=lambda: loadGame(1))
    loadButton.pack(pady=20)
    optionsButton.config(command=lambda: options(1))
    optionsButton.pack(pady=20)
    exitButton.config(command=lambda: exitDialogue(1))
    exitButton.pack(pady=20)


def newGame():
    clearScreen()
    titleLabel.pack(pady=50)
    newNameLabel.pack(pady=20)
    newNameEntry.config(textvariable=nameVar)
    newNameEntry.pack(pady=20)
    newNameButton.config(command=initGameScreen)
    newNameButton.pack(pady=20)


# draws the menu
def drawMenu():
    global boss
    clearScreen()
    boss = False
    screen.bind("<KeyPress>", move_player)
    screen.bind("<KeyRelease-" + gofwd.lower() + ">", stop_playerMovement)
    screen.bind("<KeyRelease-" + goback.lower() + ">", stop_playerMovement)
    screen.bind("<KeyRelease-" + turnleft.lower() + ">", stop_playerRotation)
    screen.bind("<KeyRelease-" + turnright.lower() + ">", stop_playerRotation)
    titleLabel.pack(pady=50)
    newButton.config(command=newGame)
    newButton.pack(pady=20)
    loadButton.config(command=lambda: loadGame(0))
    loadButton.pack(pady=20)
    optionsButton.config(command=lambda: options(0))
    optionsButton.pack(pady=20)
    leaderButton.config(command=lambda: leaderboard(0))
    leaderButton.pack(pady=20)
    exitButton.config(command=lambda: exitDialogue(0))
    exitButton.pack(pady=20)


# handles the movement of the player
def move_player(event):
    global vx, rx, hitting, paused, boss
    if(event.keysym == 'Escape'):
        if(len(mapArray) > 0):
            pauseMenu()
            paused = True
    if(event.char.lower() == '['):
        boss = True
        bossKey()
    if(event.char.lower() == gofwd):
        vx = 1
    if(event.char.lower() == goback):
        vx = -1
    if(event.char.lower() == turnright):
        rx = 1
    if(event.char.lower() == turnleft):
        rx = -1
    if(event.char.lower() == primaryAtk):
        hitting = True


# added for smoother movement

def stop_playerMovement(event):
    global vx
    vx = 0


def stop_playerRotation(event):
    global rx
    rx = 0


# generates the map configuration
def mapRandomizer():
    global mapArray, rooms, roomNumber, roomsize
    # gets the size of a single room, which needs to be odd
    roomsize = random.randint(10, 15)
    while roomsize % 2 == 0:
        roomsize = random.randint(10, 20)
        # gets the room number based on the stage
    if(stage % 2 == 1):
        roomNumber = random.randint(3, 6)
    else:
        roomNumber = random.randint(7, 9)
        # generates each room
    for room in range(roomNumber):
        tempRoom = []
        # initialises the room
        for i in range(roomsize):
            temprow = []
            for j in range(roomsize):
                temprow.append(0)
            tempRoom.append(temprow)
        # creates the outer border
        for i in range(roomsize):
            tempRoom[i][0] = 1
            tempRoom[i][roomsize - 1] = 1
        for i in range(1, roomsize-1):
            tempRoom[0][i] = 1
            tempRoom[roomsize - 1][i] = 1
        # generates some rocks for decoration
        rocknumber = random.randint(1, int(roomsize / 2))
        for i in range(rocknumber):
            x = random.randint(1, roomsize-2)
            y = random.randint(1, roomsize-2)
            while tempRoom[x][y] != 0:
                x = random.randint(1, roomsize-2)
                y = random.randint(1, roomsize-2)
            tempRoom[x][y] = 2
        tempRoom[0][int(roomsize / 2)] = 0
        tempRoom[roomsize - 1][int(roomsize / 2)] = 0
        rooms.insert(room, tempRoom)
        for line in tempRoom:
            mapArray.append(line)
    mapArray[0][int(roomsize / 2)] = 1
    mapArray[roomsize * roomNumber - 1][int(roomsize / 2)] = 1


# spawns some monsters in the room
def spawnMonsters(room):
    global monsternum
    monsternum = random.randint(1, len(mapArray[0]) - 1 + stage * 2)
    for i in range(monsternum):
        x = random.randint(2, len(mapArray[0]) - 2)
        y = random.randint(2, len(mapArray[0]) - 2)
        spriteDict['posX'].append(x + len(mapArray[0]) * room)
        spriteDict['posY'].append(y)
        spriteDict['monster'].append(True)
        spriteDict['health'].append(9 + stage)
    # locks the player in the room until all of the monsters have been killed
    mapArray[len(mapArray[0]) * room][int(len(mapArray[0]) / 2)] = 1
    mapArray[len(mapArray[0]) * (room + 1) - 1][int(len(mapArray[0]) / 2)] = 1


# initialises the game screen by generating the screen components
def initGameScreen():
    global mapArray, rooms, playerX, playerY, health, vectorX, vectorY,\
           cameraX, cameraY, paused, spriteDict, spriteOrder, name
    clearScreen()
    mapArray = []
    rooms = []
    playerX, playerY = 1.5, 1.5
    health = 100
    vectorX, vectorY = 1, 0
    cameraX, cameraY = 0, 0.66
    paused = False
    name = nameVar.get()
    spriteDict = {'posX': [], 'posY': [], 'dist': [], 'monster': [],
                  'health': []}
    spriteOrder = []
    newNameButton.focus_set()
    mainframe.pack(expand=TRUE)
    leftframe.pack(side=LEFT)
    centerframe.pack(side=LEFT)
    rightframe.pack(side=LEFT)
    minimap.pack(side=TOP)
    charphoto.pack(side=TOP)
    canvas.pack(side=TOP)
    charbottom.pack(side=TOP)
    charright.pack()
    mapRandomizer()

    # calculates the sprite positions on the map
    for i in range(len(mapArray)):
        for j in range(len(mapArray[i])):
            if(mapArray[i][j] == 2):
                spriteDict['posX'].append(i + 0.5)
                spriteDict['posY'].append(j + 0.5)
                spriteDict['monster'].append(False)
                spriteDict['health'].append(0)
    spawnMonsters(0)
    gameloop()


drawMenu()
screen.mainloop()
