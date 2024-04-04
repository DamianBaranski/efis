#include "screen.h"

Screen *Screen::instance = nullptr;

    Screen::Screen(int width, int heigth)
    {
        instance = this;
        int i = 0;
        glutInit(&i, nullptr);
        glutInitDisplayMode(GLUT_SINGLE | GLUT_RGB | GLUT_MULTISAMPLE);
        glutCreateWindow("Screen");
        glutInitWindowSize(width, heigth);
        glutInitWindowPosition(50, 50);
        glutDisplayFunc(displayWrapper);
    }

    void Screen::registerRenderer(IRenderer *renderer)
    {
        mRenderers.push_back(renderer);
    }

    void Screen::update() {
        glutPostRedisplay();
    }

    void Screen::mainLoop()
    {
        glutMainLoop();
    }
    void Screen::displayWrapper()
    {
        if (instance)
        {
            instance->render();
        }
    }

    void Screen::render()
    {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        for (auto renderer : mRenderers)
        {
            renderer->render();
        }
        glFlush(); // Render now
    }
