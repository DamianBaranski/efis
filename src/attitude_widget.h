/// \file attitude_widget.h
/// \brief Contains the declaration of the AttitudeWidget class, which represents a widget that displays attitude information.
#ifndef ATTITUDE_WIDGET_H
#define ATTITUDE_WIDGET_H

#include "iobserver.h"
#include "iwidget.h"
#include "idata_manager.h"
#include "data_type.h"
#include "render2d.h"
#include <iostream>

/// \class AttitudeWidget
/// \brief Represents a widget that displays attitude information.
///
/// This widget renders attitude information on a screen. It receives updates from a data manager
/// and renders the current attitude state accordingly.
class AttitudeWidget : public IObserver<DataType>, public IWidget
{
public:
    /// \brief Constructs an AttitudeWidget object.
    /// \param screen The screen to render the widget on.
    /// \param dataManager The data manager providing attitude data.
    AttitudeWidget(Screen &screen, IDataManager &dataManager);

    /// \brief Renders the attitude widget.
    void render() const override;

    /// \brief Sets the position of the attitude widget.
    /// \param x The x-coordinate of the position.
    /// \param y The y-coordinate of the position.
    void setPos(int x, int y) override;

    /// \brief Updates the attitude widget with new data.
    /// \param type The type of data being updated.
    void update(DataType type) override;

    /// \brief Handles mouse click events.
    /// \param x The x-coordinate of the mouse click.
    /// \param y The y-coordinate of the mouse click.
    /// \return True if the click event is handled, false otherwise.
    bool mouseClick(int x, int y) override;

private:
    IDataManager &mDataManager; ///< Reference to the data manager providing attitude data.
    Render2D mHorizon;          ///< Render2D object for the horizon.
    Render2D mScale;            ///< Render2D object for the scale.
};

#endif // ATTITUDE_WIDGET_H
