/*
  This file is part of the kcalcore library.

  SPDX-FileCopyrightText: 2001 Cornelius Schumacher <schumacher@kde.org>
  SPDX-FileCopyrightText: 2004 Reinhold Kainhofer <reinhold@kainhofer.com>

  SPDX-License-Identifier: LGPL-2.0-or-later
*/
/**
  @file
  This file is part of the API for handling calendar data and
  defines the FreeBusy class.

  @brief
  Provides information about the free/busy time of a calendar user.

  @author Cornelius Schumacher \<schumacher@kde.org\>
  @author Reinhold Kainhofer \<reinhold@kainhofer.com\>
*/
#include "freebusy.h"
#include "visitor.h"
#include "utils_p.h"

#include "icalformat.h"

#include "kcalendarcore_debug.h"
#include <QTime>

using namespace KCalendarCore;

//@cond PRIVATE
class Q_DECL_HIDDEN KCalendarCore::FreeBusy::Private
{
private:
    FreeBusy *q;
public:
    Private(FreeBusy *qq) : q(qq)
    {}

    Private(const KCalendarCore::FreeBusy::Private &other, FreeBusy *qq) : q(qq)
    {
        init(other);
    }

    Private(const FreeBusyPeriod::List &busyPeriods, FreeBusy *qq)
        : q(qq), mBusyPeriods(busyPeriods)
    {}

    void init(const KCalendarCore::FreeBusy::Private &other);
    void init(const Event::List &events, const QDateTime &start, const QDateTime &end);

    QDateTime mDtEnd;                  // end datetime
    FreeBusyPeriod::List mBusyPeriods; // list of periods

    // This is used for creating a freebusy object for the current user
    bool addLocalPeriod(FreeBusy *fb, const QDateTime &start, const QDateTime &end);
};

void KCalendarCore::FreeBusy::Private::init(const KCalendarCore::FreeBusy::Private &other)
{
    mDtEnd = other.mDtEnd;
    mBusyPeriods = other.mBusyPeriods;
}
//@endcond

FreeBusy::FreeBusy()
    : d(new KCalendarCore::FreeBusy::Private(this))
{
}

FreeBusy::FreeBusy(const FreeBusy &other)
    : IncidenceBase(other),
      d(new KCalendarCore::FreeBusy::Private(*other.d, this))
{
}

FreeBusy::FreeBusy(const QDateTime &start, const QDateTime &end)
    : d(new KCalendarCore::FreeBusy::Private(this))
{
    setDtStart(start); //NOLINT false clang-analyzer-optin.cplusplus.VirtualCall
    setDtEnd(end);     //NOLINT false clang-analyzer-optin.cplusplus.VirtualCall
}

FreeBusy::FreeBusy(const Event::List &events, const QDateTime &start, const QDateTime &end)
    : d(new KCalendarCore::FreeBusy::Private(this))
{
    setDtStart(start); //NOLINT false clang-analyzer-optin.cplusplus.VirtualCall
    setDtEnd(end);     //NOLINT false clang-analyzer-optin.cplusplus.VirtualCall

    d->init(events, start, end);
}

//@cond PRIVATE
void FreeBusy::Private::init(const Event::List &eventList,
                             const QDateTime &start, const QDateTime &end)
{
    const qint64 duration = start.daysTo(end);
    QDate day;
    QDateTime tmpStart;
    QDateTime tmpEnd;

    // Loops through every event in the calendar
    Event::List::ConstIterator it;
    for (it = eventList.constBegin(); it != eventList.constEnd(); ++it) {
        Event::Ptr event = *it;

        // If this event is transparent it shouldn't be in the freebusy list.
        if (event->transparency() == Event::Transparent) {
            continue;
        }

        // The code below can not handle all-day events. Fixing this resulted
        // in a lot of duplicated code. Instead, make a copy of the event and
        // set the period to the full day(s). This trick works for recurring,
        // multiday, and single day all-day events.
        Event::Ptr allDayEvent;
        if (event->allDay()) {
            // addDay event. Do the hack
            qCDebug(KCALCORE_LOG) << "All-day event";
            allDayEvent = Event::Ptr(new Event(*event));

            // Set the start and end times to be on midnight
            QDateTime st = allDayEvent->dtStart();
            st.setTime(QTime(0, 0));
            QDateTime nd = allDayEvent->dtEnd();
            nd.setTime(QTime(23, 59, 59, 999));
            allDayEvent->setAllDay(false);
            allDayEvent->setDtStart(st);
            allDayEvent->setDtEnd(nd);

            qCDebug(KCALCORE_LOG) << "Use:" << st.toString() << "to" << nd.toString();
            // Finally, use this event for the setting below
            event = allDayEvent;
        }

        // This whole for loop is for recurring events, it loops through
        // each of the days of the freebusy request

        for (qint64 i = 0; i <= duration; ++i) {
            day = start.addDays(i).date();
            tmpStart.setDate(day);
            tmpEnd.setDate(day);

            if (event->recurs()) {
                if (event->isMultiDay()) {
                    // FIXME: This doesn't work for sub-daily recurrences or recurrences with
                    //        a different time than the original event.
                    const qint64 extraDays = event->dtStart().daysTo(event->dtEnd());
                    for (qint64 x = 0; x <= extraDays; ++x) {
                        if (event->recursOn(day.addDays(-x), start.timeZone())) {
                            tmpStart.setDate(day.addDays(-x));
                            tmpStart.setTime(event->dtStart().time());
                            tmpEnd = event->duration().end(tmpStart);

                            addLocalPeriod(q, tmpStart, tmpEnd);
                            break;
                        }
                    }
                } else {
                    if (event->recursOn(day, start.timeZone())) {
                        tmpStart.setTime(event->dtStart().time());
                        tmpEnd.setTime(event->dtEnd().time());

                        addLocalPeriod(q, tmpStart, tmpEnd);
                    }
                }
            }
        }

        // Non-recurring events
        addLocalPeriod(q, event->dtStart(), event->dtEnd());
    }

    q->sortList();
}
//@endcond

FreeBusy::FreeBusy(const Period::List &busyPeriods)
    : d(new KCalendarCore::FreeBusy::Private(this))
{
    addPeriods(busyPeriods);
}

FreeBusy::FreeBusy(const FreeBusyPeriod::List &busyPeriods)
    : d(new KCalendarCore::FreeBusy::Private(busyPeriods, this))
{
}

FreeBusy::~FreeBusy()
{
    delete d;
}

IncidenceBase::IncidenceType FreeBusy::type() const
{
    return TypeFreeBusy;
}

QByteArray FreeBusy::typeStr() const
{
    return QByteArrayLiteral("FreeBusy");
}

void FreeBusy::setDtStart(const QDateTime &start)
{
    IncidenceBase::setDtStart(start.toUTC());
    updated();
}

void FreeBusy::setDtEnd(const QDateTime &end)
{
    d->mDtEnd = end;
}

QDateTime FreeBusy::dtEnd() const
{
    return d->mDtEnd;
}

Period::List FreeBusy::busyPeriods() const
{
    Period::List res;

    res.reserve(d->mBusyPeriods.count());
    for (const FreeBusyPeriod &p : qAsConst(d->mBusyPeriods)) {
        res << p;
    }

    return res;
}

FreeBusyPeriod::List FreeBusy::fullBusyPeriods() const
{
    return d->mBusyPeriods;
}

void FreeBusy::sortList()
{
    std::sort(d->mBusyPeriods.begin(), d->mBusyPeriods.end());
}

void FreeBusy::addPeriods(const Period::List &list)
{
    d->mBusyPeriods.reserve(d->mBusyPeriods.count() + list.count());
    for (const Period &p : qAsConst(list)) {
        d->mBusyPeriods << FreeBusyPeriod(p);
    }
    sortList();
}

void FreeBusy::addPeriods(const FreeBusyPeriod::List &list)
{
    d->mBusyPeriods += list;
    sortList();
}

void FreeBusy::addPeriod(const QDateTime &start, const QDateTime &end)
{
    d->mBusyPeriods.append(FreeBusyPeriod(start, end));
    sortList();
}

void FreeBusy::addPeriod(const QDateTime &start, const Duration &duration)
{
    d->mBusyPeriods.append(FreeBusyPeriod(start, duration));
    sortList();
}

void FreeBusy::merge(const FreeBusy::Ptr &freeBusy)
{
    if (freeBusy->dtStart() < dtStart()) {
        setDtStart(freeBusy->dtStart());
    }

    if (freeBusy->dtEnd() > dtEnd()) {
        setDtEnd(freeBusy->dtEnd());
    }

    Period::List periods = freeBusy->busyPeriods();
    Period::List::ConstIterator it;
    d->mBusyPeriods.reserve(d->mBusyPeriods.count() + periods.count());
    for (it = periods.constBegin(); it != periods.constEnd(); ++it) {
        d->mBusyPeriods.append(FreeBusyPeriod((*it).start(), (*it).end()));
    }
    sortList();
}

void FreeBusy::shiftTimes(const QTimeZone &oldZone, const QTimeZone &newZone)
{
    if (oldZone.isValid() && newZone.isValid() && oldZone != newZone) {
        IncidenceBase::shiftTimes(oldZone, newZone);
        d->mDtEnd = d->mDtEnd.toTimeZone(oldZone);
        d->mDtEnd.setTimeZone(newZone);
        for (FreeBusyPeriod p : qAsConst(d->mBusyPeriods)) {
            p.shiftTimes(oldZone, newZone);
        }
    }
}

IncidenceBase &FreeBusy::assign(const IncidenceBase &other)
{
    if (&other != this) {
        IncidenceBase::assign(other);
        const FreeBusy *f = static_cast<const FreeBusy *>(&other);
        d->init(*(f->d));
    }
    return *this;
}

bool FreeBusy::equals(const IncidenceBase &freeBusy) const
{
    if (!IncidenceBase::equals(freeBusy)) {
        return false;
    } else {
        // If they weren't the same type IncidenceBase::equals would had returned false already
        const FreeBusy *fb = static_cast<const FreeBusy *>(&freeBusy);
        return
            dtEnd() == fb->dtEnd() &&
            d->mBusyPeriods == fb->d->mBusyPeriods;
    }
}

bool FreeBusy::accept(Visitor &v, const IncidenceBase::Ptr &incidence)
{
    return v.visit(incidence.staticCast<FreeBusy>());
}

QDateTime FreeBusy::dateTime(DateTimeRole role) const
{
    Q_UNUSED(role);
    // No roles affecting freeBusy yet
    return QDateTime();
}

void FreeBusy::setDateTime(const QDateTime &dateTime, DateTimeRole role)
{
    Q_UNUSED(dateTime);
    Q_UNUSED(role);
}

void FreeBusy::virtual_hook(VirtualHook id, void *data)
{
    Q_UNUSED(id);
    Q_UNUSED(data);
    Q_ASSERT(false);
}

//@cond PRIVATE
bool FreeBusy::Private::addLocalPeriod(FreeBusy *fb,
                                       const QDateTime &eventStart,
                                       const QDateTime &eventEnd)
{
    QDateTime tmpStart;
    QDateTime tmpEnd;

    //Check to see if the start *or* end of the event is
    //between the start and end of the freebusy dates.
    QDateTime start = fb->dtStart();
    if (!(((start.secsTo(eventStart) >= 0) &&
            (eventStart.secsTo(mDtEnd) >= 0)) ||
            ((start.secsTo(eventEnd) >= 0) &&
             (eventEnd.secsTo(mDtEnd) >= 0)))) {
        return false;
    }

    if (eventStart.secsTo(start) >= 0) {
        tmpStart = start;
    } else {
        tmpStart = eventStart;
    }

    if (eventEnd.secsTo(mDtEnd) <= 0) {
        tmpEnd = mDtEnd;
    } else {
        tmpEnd = eventEnd;
    }

    FreeBusyPeriod p(tmpStart, tmpEnd);
    mBusyPeriods.append(p);

    return true;
}
//@endcond

QLatin1String FreeBusy::mimeType() const
{
    return FreeBusy::freeBusyMimeType();
}

QLatin1String KCalendarCore::FreeBusy::freeBusyMimeType()
{
    return QLatin1String("application/x-vnd.akonadi.calendar.freebusy");
}

QDataStream &KCalendarCore::operator<<(QDataStream &stream, const KCalendarCore::FreeBusy::Ptr &freebusy)
{
    KCalendarCore::ICalFormat format;
    QString data = format.createScheduleMessage(freebusy, iTIPPublish);
    return stream << data;
}

QDataStream &KCalendarCore::operator>>(QDataStream &stream, KCalendarCore::FreeBusy::Ptr &freebusy)
{
    QString freeBusyVCal;
    stream >> freeBusyVCal;

    KCalendarCore::ICalFormat format;
    freebusy = format.parseFreeBusy(freeBusyVCal);

    if (!freebusy) {
        qCDebug(KCALCORE_LOG) << "Error parsing free/busy";
        qCDebug(KCALCORE_LOG) << freeBusyVCal;
    }

    return stream;
}

