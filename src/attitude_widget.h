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
class AttitudeWidget : public IObserver<DataType>, public IWidget {
public:
    /// \brief Constructs an AttitudeWidget object.
    /// \param screen The screen to render the widget on.
    /// \param dataManager The data manager providing attitude data.
    AttitudeWidget(Screen &screen, IDataManager &dataManager);

    /// \brief Renders the attitude widget.
    void render() const override;

    /// \brief Enables or disables the attitude widget.
    /// \param enable True to enable the widget, false to disable it.
    void enable(bool enable) override;

    /// \brief Sets the position of the attitude widget.
    /// \param x The x-coordinate of the position.
    /// \param y The y-coordinate of the position.
    void setPos(int x, int y) override;

    /// \brief Updates the attitude widget with new data.
    /// \param type The type of data being updated.
    void update(DataType type) override;

private:
    Render2D mScale;
    IDataManager &mDataManager;
   
};

#endif // ATTITUDE_WIDGET_H
