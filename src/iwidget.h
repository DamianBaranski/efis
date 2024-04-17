/// \file iwidget.h
/// \brief Contains the declaration of the IWidget class.
#ifndef IWIDGET_H
#define IWIDGET_H

#include "screen.h"

/// \class IWidget
/// \brief The base interface class for all widgets.
///
/// This class defines the common interface for all widgets.
/// Widgets are components that can be rendered on a screen.
class IWidget : public IRenderer {
public:
    /// \brief Constructs a new IWidget object.
    ///
    /// \param screen The screen on which the widget will be rendered.
    IWidget(Screen& screen) : mScreen(screen) {mScreen.registerRenderer(this);}

    /// \brief Renders the widget.
    ///
    /// This method must be implemented by derived classes to render the widget.
    virtual void render() const = 0;

    /// \brief Enables or disables the widget.
    ///
    /// \param enable If true, the widget is enabled; otherwise, it is disabled.
    virtual void enable(bool enable) = 0;

    /// \brief Sets the position of the widget.
    ///
    /// \param x The x-coordinate of the widget's position.
    /// \param y The y-coordinate of the widget's position.
    virtual void setPos(int x, int y) = 0;

protected:
    Screen& mScreen; ///< The screen on which the widget is rendered.
};

#endif // IWIDGET_H
