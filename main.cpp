#include <iostream>
#include <conio.h>
#include <cstdlib>

const int WIDTH = 16;
const int HEIGHT = 16;

int playerX = WIDTH / 2;
int playerY = HEIGHT / 2;

void drawGrid() {
    system("cls");

    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            if (x == playerX && y == playerY)
                std::cout << "@";
            else
                std::cout << ".";
        }
        std::cout << "\n";
    }
}

void handleInput() {
    char key = _getch();

    if (key == 'w' && playerY > 0) playerY--;
    else if (key == 's' && playerY < HEIGHT - 1) playerY++;
    else if (key == 'a' && playerX > 0) playerX--;
    else if (key == 'd' && playerX < WIDTH - 1) playerX++;
}

int main() {
    while (true) {
        drawGrid();
        handleInput();
    }
    return 0;
}