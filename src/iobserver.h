/// \file iobserver.h
/// \brief Contains the declaration of the IObserver class.
#ifndef IOBSERVER_H
#define IOBSERVER_H

/// \class IObserver
/// \brief Interface for Observer pattern.
///
/// @tparam T The type of data being observed.
template <typename T>
class IObserver {
public:
    virtual ~IObserver() {}

    /// Update method called by the subject to notify the observer of changes.
    ///
    /// @param type The updated data.
    virtual void update(T type) = 0;
};
/// \class ISubject
/// \brief Interface for Subject pattern.
///
/// @tparam T The type of data being observed.
template <typename T>
class ISubject {
public:
    virtual ~ISubject() {}

    /// Attaches an observer to the subject.
    ///
    /// @param observer Pointer to the observer to attach.
    /// @param type The type of data the observer is interested in.
    virtual void attach(IObserver<T>* observer, const T& type) = 0;

    /// Detaches an observer from the subject.
    ///
    /// @param observer Pointer to the observer to detach.
    virtual void detach(IObserver<T>* observer) = 0;

protected:
    /// Notifies all attached observers of changes in data.
    ///
    /// @param type The type of data that has changed.
    virtual void notify(const T& type) const = 0;
};

#endif
