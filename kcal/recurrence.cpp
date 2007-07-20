/*
    This file is part of libkcal.

    Copyright (c) 1998 Preston Brown <pbrown@kde.org>
    Copyright (c) 2001 Cornelius Schumacher <schumacher@kde.org>
    Copyright (c) 2002,2006 David Jarvie <software@astrojar.org.uk>
    Copyright (C) 2005 Reinhold Kainhofer <kainhofer@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "recurrence.h"

#include "recurrencerule.h"

#include <limits.h>

#include <QtCore/QList>
#include <QtCore/QBitArray>

#include <kglobal.h>
#include <klocale.h>
#include <kdebug.h>

using namespace KCal;

//@cond PRIVATE
class KCal::Recurrence::Private
{
  public:
    Private()
      : mCachedType( rMax ),
        mFloating( false ),
        mRecurReadOnly( false )
    {
        mExRules.setAutoDelete( true );
        mRRules.setAutoDelete( true );
    }

    Private( const Private &p )
      : mRDateTimes( p.mRDateTimes ),
        mRDates( p.mRDates ),
        mExDateTimes( p.mExDateTimes ),
        mExDates( p.mExDates ),
        mStartDateTime( p.mStartDateTime ),
        mCachedType( p.mCachedType ),
        mFloating( p.mFloating ),
        mRecurReadOnly(p.mRecurReadOnly)
    {
        mExRules.setAutoDelete( true );
        mRRules.setAutoDelete( true );
    }

    bool operator==( const Private &p ) const;

    RecurrenceRule::List mExRules;
    RecurrenceRule::List mRRules;
    DateTimeList mRDateTimes;
    DateList     mRDates;
    DateTimeList mExDateTimes;
    DateList     mExDates;
    KDateTime mStartDateTime;    // date/time of first recurrence
    QList<RecurrenceObserver*> mObservers;

    // Cache the type of the recurrence with the old system (e.g. MonthlyPos)
    mutable ushort mCachedType;

    bool mFloating;              // the recurrence has no time, just a date
    bool mRecurReadOnly;
};
//@endcond

bool Recurrence::Private::operator==( const Recurrence::Private &p ) const
{
  if ( mStartDateTime != p.mStartDateTime
  ||   mFloating != p.mFloating
  ||   mRecurReadOnly != p.mRecurReadOnly
  ||   mExDates != p.mExDates
  ||   mExDateTimes != p.mExDateTimes
  ||   mRDates != p.mRDates
  ||   mRDateTimes != p.mRDateTimes ) {
    return false;
  }

// Compare the rrules, exrules! Assume they have the same order... This only
// matters if we have more than one rule (which shouldn't be the default anyway)
  int i;
  int end = mRRules.count();
  if ( end != p.mRRules.count() ) {
    return false;
  }
  for ( i = 0;  i < end;  ++i ) {
    if ( *mRRules[i] != *p.mRRules[i] )  return false;
  }
  end = mExRules.count();
  if ( end != p.mExRules.count() ) {
    return false;
  }
  for ( i = 0;  i < end;  ++i ) {
    if ( *mExRules[i] != *p.mExRules[i] )  return false;
  }
  return true;
}


Recurrence::Recurrence()
  : d( new KCal::Recurrence::Private() )
{
}

Recurrence::Recurrence( const Recurrence &r )
  : RecurrenceRule::RuleObserver(),
    d( new KCal::Recurrence::Private( *r.d ) )
{
  int i, end;
  for ( i = 0, end = r.d->mRRules.count();  i < end;  ++i ) {
    RecurrenceRule *rule = new RecurrenceRule( *r.d->mRRules[i] );
    d->mRRules.append( rule );
    rule->addObserver( this );
  }
  for ( i = 0, end = r.d->mExRules.count();  i < end;  ++i ) {
    RecurrenceRule *rule = new RecurrenceRule( *r.d->mExRules[i] );
    d->mExRules.append( rule );
    rule->addObserver( this );
  }
}

Recurrence::~Recurrence()
{
  delete d;
}



bool Recurrence::operator==( const Recurrence& r2 ) const
{
  return *d == *r2.d;
}

void Recurrence::addObserver( RecurrenceObserver *observer )
{
  if ( !d->mObservers.contains( observer ) )
    d->mObservers.append( observer );
}

void Recurrence::removeObserver( RecurrenceObserver *observer )
{
  if ( d->mObservers.contains( observer ) )
    d->mObservers.removeAll( observer );
}


KDateTime Recurrence::startDateTime() const
{
  return d->mStartDateTime;
}

bool Recurrence::floats() const
{
  return d->mFloating;
}

void Recurrence::setFloats( bool floats )
{
  if ( d->mRecurReadOnly || floats == d->mFloating )
    return;
  d->mFloating = floats;

  for ( int i = 0, end = d->mRRules.count();  i < end;  ++i ) {
    d->mRRules[i]->setFloats( floats );
  }
  for ( int i = 0, end = d->mExRules.count();  i < end;  ++i ) {
    d->mExRules[i]->setFloats( floats );
  }
  updated();
}

RecurrenceRule *Recurrence::defaultRRule( bool create ) const
{
  if ( d->mRRules.isEmpty() ) {
    if ( !create || d->mRecurReadOnly )
      return 0;
    RecurrenceRule *rrule = new RecurrenceRule();
    rrule->setStartDt( startDateTime() );
    const_cast<KCal::Recurrence*>(this)->addRRule( rrule );
    return rrule;
  } else {
    return d->mRRules[0];
  }
}

RecurrenceRule *Recurrence::defaultRRuleConst() const
{
  return d->mRRules.isEmpty() ? 0 : d->mRRules[0];
}

void Recurrence::updated()
{
  // recurrenceType() re-calculates the type if it's rMax
  d->mCachedType = rMax;
  for ( int i = 0, end = d->mObservers.count();  i < end;  ++i ) {
    if ( d->mObservers[i] )
      d->mObservers[i]->recurrenceUpdated( this );
  }
}

bool Recurrence::recurs() const
{
  return !d->mRRules.isEmpty() || !d->mRDates.isEmpty() || !d->mRDateTimes.isEmpty();
}

ushort Recurrence::recurrenceType() const
{
  if ( d->mCachedType == rMax ) {
    d->mCachedType = recurrenceType( defaultRRuleConst() );
  }
  return d->mCachedType;
}

ushort Recurrence::recurrenceType( const RecurrenceRule *rrule )
{
  if ( !rrule )
    return rNone;
  RecurrenceRule::PeriodType type = rrule->recurrenceType();

  // BYSETPOS, BYWEEKNUMBER and BYSECOND were not supported in old versions
  if ( !rrule->bySetPos().isEmpty()
  ||   !rrule->bySeconds().isEmpty()
  ||   !rrule->byWeekNumbers().isEmpty() )
    return rOther;

  // It wasn't possible to set BYMINUTES, BYHOUR etc. by the old code. So if
  // it's set, it's none of the old types
  if ( !rrule->byMinutes().isEmpty()
  ||   !rrule->byHours().isEmpty() )
    return rOther;

  // Possible combinations were:
  // BYDAY: with WEEKLY, MONTHLY, YEARLY
  // BYMONTHDAY: with MONTHLY, YEARLY
  // BYMONTH: with YEARLY
  // BYYEARDAY: with YEARLY
  if ( !rrule->byYearDays().isEmpty() && type != RecurrenceRule::rYearly
  ||   !rrule->byMonths().isEmpty() && type != RecurrenceRule::rYearly )
    return rOther;
  if ( !rrule->byDays().isEmpty() ) {
    if ( type != RecurrenceRule::rYearly && type != RecurrenceRule::rMonthly &&
         type != RecurrenceRule::rWeekly )
      return rOther;
  }

  switch ( type ) {
    case RecurrenceRule::rNone:     return rNone;
    case RecurrenceRule::rMinutely: return rMinutely;
    case RecurrenceRule::rHourly:   return rHourly;
    case RecurrenceRule::rDaily:    return rDaily;
    case RecurrenceRule::rWeekly:   return rWeekly;
    case RecurrenceRule::rMonthly: {
        if ( rrule->byDays().isEmpty() ) return rMonthlyDay;
        else if ( rrule->byMonthDays().isEmpty() ) return rMonthlyPos;
        else return rOther; // both position and date specified
      }
    case RecurrenceRule::rYearly: {
        // Possible combinations:
        //   rYearlyMonth: [BYMONTH &] BYMONTHDAY
        //   rYearlyDay: BYYEARDAY
        //   rYearlyPos: [BYMONTH &] BYDAY
        if ( !rrule->byDays().isEmpty() ) {
          // can only by rYearlyPos
          if ( rrule->byMonthDays().isEmpty() && rrule->byYearDays().isEmpty() )
            return rYearlyPos;
          else return rOther;
        } else if ( !rrule->byYearDays().isEmpty() ) {
          // Can only be rYearlyDay
          if ( rrule->byMonths().isEmpty() && rrule->byMonthDays().isEmpty() )
            return rYearlyDay;
          else return rOther;
        } else {
          return rYearlyMonth;
        }
        break;
      }
     default: return rOther;
  }
  return rOther;
}

bool Recurrence::recursOn(const QDate &qd, const KDateTime::Spec &timeSpec) const
{
  // Don't waste time if date is before the start of the recurrence
  if ( KDateTime( qd, QTime(23,59,59), timeSpec ) < d->mStartDateTime )
    return false;

  // First handle dates. Exrules override
  if ( d->mExDates.containsSorted( qd ) )
    return false;
  int i, end;
  TimeList tms;
  // For all-day events a matching exrule excludes the whole day
  // since exclusions take precedence over inclusions, we know it can't occur on that day.
  if ( floats() ) {
    for ( i = 0, end = d->mExRules.count();  i < end;  ++i ) {
      if ( d->mExRules[i]->recursOn( qd, timeSpec ) )
        return false;
    }
  }

  if ( d->mRDates.containsSorted( qd ) )
    return true;

  // Check if it might recur today at all.
  bool recurs = ( startDate() == qd );
  for ( i = 0, end = d->mRDateTimes.count();  i < end && !recurs;  ++i ) {
    recurs = ( d->mRDateTimes[i].toTimeSpec( timeSpec ).date() == qd );
  }
  for ( i = 0, end = d->mRRules.count();  i < end && !recurs;  ++i ) {
    recurs = d->mRRules[i]->recursOn( qd, timeSpec );
  }
  // If the event wouldn't recur at all, simply return false, don't check ex*
  if ( !recurs )
    return false;

  // Check if there are any times for this day excluded, either by exdate or exrule:
  bool exon = false;
  for ( i = 0, end = d->mExDateTimes.count();  i < end && !exon;  ++i ) {
    exon = ( d->mExDateTimes[i].toTimeSpec( timeSpec ).date() == qd );
  }
  if ( !floats() ) {     // we have already checked floating times above
    for ( i = 0, end = d->mExRules.count();  i < end && !exon;  ++i ) {
      exon = d->mExRules[i]->recursOn( qd, timeSpec );
    }
  }

  if ( !exon ) {
    // Simple case, nothing on that day excluded, return the value from before
    return recurs;
  } else {
    // Harder part: I don't think there is any way other than to calculate the
    // whole list of items for that day.
//TODO: consider whether it would be more efficient to call
//      Rule::recurTimesOn() instead of Rule::recursOn() from the start
    TimeList timesForDay( recurTimesOn( qd, timeSpec ) );
    return !timesForDay.isEmpty();
  }
}

bool Recurrence::recursAt( const KDateTime &dt ) const
{
  // Convert to recurrence's time zone for date comparisons, and for more efficient time comparisons
  KDateTime dtrecur = dt.toTimeSpec( d->mStartDateTime.timeSpec() );

  // if it's excluded anyway, don't bother to check if it recurs at all.
  if ( d->mExDateTimes.containsSorted( dtrecur )
  ||   d->mExDates.containsSorted( dtrecur.date() ) )
    return false;
  int i, end;
  for ( i = 0, end = d->mExRules.count();  i < end;  ++i ) {
    if ( d->mExRules[i]->recursAt( dtrecur ) ) return false;
  }

  // Check explicit recurrences, then rrules.
  if ( startDateTime() == dtrecur || d->mRDateTimes.containsSorted( dtrecur ) )
    return true;
  for ( i = 0, end = d->mRRules.count();  i < end;  ++i ) {
    if ( d->mRRules[i]->recursAt( dtrecur ) ) return true;
  }

  return false;
}

/** Calculates the cumulative end of the whole recurrence (rdates and rrules).
    If any rrule is infinite, or the recurrence doesn't have any rrules or
    rdates, an invalid date is returned. */
KDateTime Recurrence::endDateTime() const
{
  DateTimeList dts;
  dts << startDateTime();
  if ( !d->mRDates.isEmpty() ) {
    dts << KDateTime( d->mRDates.last(), QTime( 0, 0, 0 ), d->mStartDateTime.timeSpec() );
  }
  if ( !d->mRDateTimes.isEmpty() ) {
    dts << d->mRDateTimes.last();
  }
  for ( int i = 0, end = d->mRRules.count();  i < end;  ++i ) {
    KDateTime rl( d->mRRules[i]->endDt() );
    // if any of the rules is infinite, the whole recurrence is
    if ( !rl.isValid() )
      return KDateTime();
    dts << rl;
  }
  dts.sortUnique();
  return dts.isEmpty() ? KDateTime() : dts.last();
}

/** Calculates the cumulative end of the whole recurrence (rdates and rrules).
    If any rrule is infinite, or the recurrence doesn't have any rrules or
    rdates, an invalid date is returned. */
QDate Recurrence::endDate() const
{
  KDateTime end( endDateTime() );
  return end.isValid() ? end.date() : QDate();
}

void Recurrence::setEndDate( const QDate &date )
{
  KDateTime dt( date, d->mStartDateTime.time(), d->mStartDateTime.timeSpec() );
  if ( floats() )
    dt.setTime( QTime( 23, 59, 59 ) );
  setEndDateTime( dt );
}

void Recurrence::setEndDateTime( const KDateTime &dateTime )
{
  if ( d->mRecurReadOnly ) return;
  RecurrenceRule *rrule = defaultRRule( true );
  if ( !rrule ) return;
  rrule->setEndDt( dateTime );
  updated();
}

int Recurrence::duration() const
{
  RecurrenceRule *rrule = defaultRRuleConst();
  return rrule ? rrule->duration() : 0;
}

int Recurrence::durationTo( const KDateTime &datetime ) const
{
  // Emulate old behavior: This is just an interface to the first rule!
  RecurrenceRule *rrule = defaultRRuleConst();
  return rrule ? rrule->durationTo( datetime ) : 0;
}

int Recurrence::durationTo( const QDate &date ) const
{
  return durationTo( KDateTime( date, QTime( 23, 59, 59 ), d->mStartDateTime.timeSpec() ) );
}

void Recurrence::setDuration( int duration )
{
  if ( d->mRecurReadOnly ) return;
  RecurrenceRule *rrule = defaultRRule( true );
  if ( !rrule ) return;
  rrule->setDuration( duration );
  updated();
}

void Recurrence::shiftTimes(const KDateTime::Spec &oldSpec, const KDateTime::Spec &newSpec)
{
  if ( d->mRecurReadOnly ) return;
  d->mStartDateTime = d->mStartDateTime.toTimeSpec( oldSpec );
  d->mStartDateTime.setTimeSpec( newSpec );

  int i, end;
  for ( i = 0, end = d->mRDateTimes.count();  i < end;  ++i ) {
    d->mRDateTimes[i] = d->mRDateTimes[i].toTimeSpec( oldSpec );
    d->mRDateTimes[i].setTimeSpec( newSpec );
  }
  for ( i = 0, end = d->mExDateTimes.count();  i < end;  ++i ) {
    d->mExDateTimes[i] = d->mExDateTimes[i].toTimeSpec( oldSpec );
    d->mExDateTimes[i].setTimeSpec( newSpec );
  }
  for ( i = 0, end = d->mRRules.count();  i < end;  ++i )
    d->mRRules[i]->shiftTimes( oldSpec, newSpec );
  for ( i = 0, end = d->mExRules.count();  i < end;  ++i )
    d->mExRules[i]->shiftTimes( oldSpec, newSpec );
}

void Recurrence::unsetRecurs()
{
  if ( d->mRecurReadOnly ) return;
  d->mRRules.clear();
  updated();
}

void Recurrence::clear()
{
  if ( d->mRecurReadOnly ) return;
  d->mRRules.clear();
  d->mExRules.clear();
  d->mRDates.clear();
  d->mRDateTimes.clear();
  d->mExDates.clear();
  d->mExDateTimes.clear();
  d->mCachedType = rMax;
  updated();
}

void Recurrence::setRecurReadOnly( bool readOnly )
{
  d->mRecurReadOnly = readOnly;
}

bool Recurrence::recurReadOnly() const
{
  return d->mRecurReadOnly;
}

QDate Recurrence::startDate() const
{
  return d->mStartDateTime.date();
}

void Recurrence::setStartDateTime( const KDateTime &start )
{
  if ( d->mRecurReadOnly ) return;
  d->mStartDateTime = start;
  if ( start.isDateOnly() )
    setFloats( true );
  else {
    setFloats( false );   // set all RRULEs and EXRULEs

    int i, end;
    for ( i = 0, end = d->mRRules.count();  i < end;  ++i ) {
      d->mRRules[i]->setStartDt( start );
    }
    for ( i = 0, end = d->mExRules.count();  i < end;  ++i ) {
      d->mExRules[i]->setStartDt( start );
    }
  }
  updated();
}

int Recurrence::frequency() const
{
  RecurrenceRule *rrule = defaultRRuleConst();
  return rrule ? rrule->frequency() : 0;
}

// Emulate the old behaviour. Make this methods just an interface to the
// first rrule
void Recurrence::setFrequency( int freq )
{
  if ( d->mRecurReadOnly || freq <= 0 ) return;
  RecurrenceRule *rrule = defaultRRule( true );
  if ( rrule )
    rrule->setFrequency( freq );
  updated();
}


// WEEKLY

int Recurrence::weekStart() const
{
  RecurrenceRule *rrule = defaultRRuleConst();
  return rrule ? rrule->weekStart() : 1;
}

// Emulate the old behavior
QBitArray Recurrence::days() const
{
  QBitArray days( 7 );
  days.fill( 0 );
  RecurrenceRule *rrule = defaultRRuleConst();
  if ( rrule ) {
    QList<RecurrenceRule::WDayPos> bydays = rrule->byDays();
    for (int i = 0; i < bydays.size(); ++i) {
      if( bydays.at(i).pos() == 0)
        days.setBit( bydays.at(i).day() - 1 );
    }
  }
  return days;
}


// MONTHLY

// Emulate the old behavior
QList<int> Recurrence::monthDays() const
{
  RecurrenceRule *rrule = defaultRRuleConst();
  if ( rrule ) return rrule->byMonthDays();
  else return QList<int>();
}

// Emulate the old behavior
QList<RecurrenceRule::WDayPos> Recurrence::monthPositions() const
{
  RecurrenceRule *rrule = defaultRRuleConst();
  return rrule ? rrule->byDays() : QList<RecurrenceRule::WDayPos>();
}


// YEARLY

QList<int> Recurrence::yearDays() const
{
  RecurrenceRule *rrule = defaultRRuleConst();
  return rrule ? rrule->byYearDays() : QList<int>();
}

QList<int> Recurrence::yearDates() const
{
  return monthDays();
}

QList<int> Recurrence::yearMonths() const
{
  RecurrenceRule *rrule = defaultRRuleConst();
  return rrule ? rrule->byMonths() : QList<int>();
}

QList<RecurrenceRule::WDayPos> Recurrence::yearPositions() const
{
  return monthPositions();
}



RecurrenceRule *Recurrence::setNewRecurrenceType( RecurrenceRule::PeriodType type, int freq )
{
  if ( d->mRecurReadOnly || freq <= 0 ) return 0;
  d->mRRules.clear();
  updated();
  RecurrenceRule *rrule = defaultRRule( true );
  if ( !rrule ) return 0;
  rrule->setRecurrenceType( type );
  rrule->setFrequency( freq );
  rrule->setDuration( -1 );
  return rrule;
}

void Recurrence::setMinutely( int _rFreq )
{
  if ( setNewRecurrenceType( RecurrenceRule::rMinutely, _rFreq ) )
    updated();
}

void Recurrence::setHourly( int _rFreq )
{
  if ( setNewRecurrenceType( RecurrenceRule::rHourly, _rFreq ) )
    updated();
}

void Recurrence::setDaily( int _rFreq )
{
  if ( setNewRecurrenceType( RecurrenceRule::rDaily, _rFreq ) )
    updated();
}

void Recurrence::setWeekly( int freq, int weekStart )
{
  RecurrenceRule *rrule = setNewRecurrenceType( RecurrenceRule::rWeekly, freq );
  if ( !rrule ) return;
  rrule->setWeekStart( weekStart );
  updated();
}

void Recurrence::setWeekly( int freq, const QBitArray &days, int weekStart )
{
  setWeekly( freq, weekStart );
  addMonthlyPos( 0, days );
}

void Recurrence::addWeeklyDays( const QBitArray &days )
{
  addMonthlyPos( 0, days );
}

void Recurrence::setMonthly( int freq )
{
  if ( setNewRecurrenceType( RecurrenceRule::rMonthly, freq ) )
    updated();
}

void Recurrence::addMonthlyPos( short pos, const QBitArray &days )
{
  // Allow 53 for yearly!
  if ( d->mRecurReadOnly || pos > 53 || pos < -53 ) return;
  RecurrenceRule *rrule = defaultRRule( false );
  if ( !rrule ) return;
  bool changed = false;
  QList<RecurrenceRule::WDayPos> positions = rrule->byDays();

  for ( int i = 0; i < 7; ++i ) {
    if ( days.testBit(i) ) {
      RecurrenceRule::WDayPos p( pos, i + 1 );
      if ( !positions.contains( p ) ) {
        changed = true;
        positions.append( p );
      }
    }
  }
  if ( changed ) {
    rrule->setByDays( positions );
    updated();
  }
}


void Recurrence::addMonthlyPos( short pos, ushort day )
{
  // Allow 53 for yearly!
  if ( d->mRecurReadOnly || pos > 53 || pos < -53 ) return;
  RecurrenceRule *rrule = defaultRRule( false );
  if ( !rrule ) return;
  QList<RecurrenceRule::WDayPos> positions = rrule->byDays();

  RecurrenceRule::WDayPos p( pos, day );
  if ( !positions.contains( p ) ) {
    positions.append( p );
    rrule->setByDays( positions );
    updated();
  }
}


void Recurrence::addMonthlyDate( short day )
{
  if ( d->mRecurReadOnly || day > 31 || day < -31 ) return;
  RecurrenceRule *rrule = defaultRRule( true );
  if ( !rrule ) return;

  QList<int> monthDays = rrule->byMonthDays();
  if ( !monthDays.contains( day ) ) {
    monthDays.append( day );
    rrule->setByMonthDays( monthDays );
    updated();
  }
}

void Recurrence::setYearly( int freq )
{
  if ( setNewRecurrenceType( RecurrenceRule::rYearly, freq ) )
    updated();
}


// Daynumber within year
void Recurrence::addYearlyDay( int day )
{
  RecurrenceRule *rrule = defaultRRule( false ); // It must already exist!
  if ( !rrule ) return;

  QList<int> days = rrule->byYearDays();
  if ( !days.contains( day ) ) {
    days << day;
    rrule->setByYearDays( days );
    updated();
  }
}

// day part of date within year
void Recurrence::addYearlyDate( int day )
{
  addMonthlyDate( day );
}

// day part of date within year, given as position (n-th weekday)
void Recurrence::addYearlyPos( short pos, const QBitArray &days )
{
  addMonthlyPos( pos, days );
}


// month part of date within year
void Recurrence::addYearlyMonth( short month )
{
  if ( d->mRecurReadOnly || month < 1 || month > 12 ) return;
  RecurrenceRule *rrule = defaultRRule( false );
  if ( !rrule ) return;

  QList<int> months = rrule->byMonths();
  if ( !months.contains(month) ) {
    months << month;
    rrule->setByMonths( months );
    updated();
  }
}


TimeList Recurrence::recurTimesOn( const QDate &date, const KDateTime::Spec &timeSpec ) const
{
// kDebug(5800) << "recurTimesOn(" << date << ")" << endl;
  int i, end;
  TimeList times;
  // The whole day is excepted
  if ( d->mExDates.containsSorted( date ) ) return times;
  // EXRULE takes precedence over RDATE entries, so for floating events,
  // a matching excule also excludes the whole day automatically
  if ( floats() ) {
    for ( i = 0, end = d->mExRules.count();  i < end;  ++i ) {
      if ( d->mExRules[i]->recursOn( date, timeSpec ) )
        return times;
    }
  }

  KDateTime dt = startDateTime().toTimeSpec( timeSpec );
  if ( dt.date() == date )
    times << dt.time();
  bool foundDate = false;
  for ( i = 0, end = d->mRDateTimes.count();  i < end;  ++i ) {
    dt = d->mRDateTimes[i].toTimeSpec( timeSpec );
    if ( dt.date() == date ) {
      times << dt.time();
      foundDate = true;
    } else if (foundDate) break; // <= Assume that the rdatetime list is sorted
  }
  for ( i = 0, end = d->mRRules.count();  i < end;  ++i ) {
    times += d->mRRules[i]->recurTimesOn( date, timeSpec );
  }
  times.sortUnique();

  foundDate = false;
  TimeList extimes;
  for ( i = 0, end = d->mExDateTimes.count();  i < end;  ++i ) {
    dt = d->mExDateTimes[i].toTimeSpec( timeSpec );
    if ( dt.date() == date ) {
      extimes << dt.time();
      foundDate = true;
    } else if (foundDate) break;
  }
  if ( !floats() ) {     // we have already checked floating times above
    for ( i = 0, end = d->mExRules.count();  i < end;  ++i ) {
      extimes += d->mExRules[i]->recurTimesOn( date, timeSpec );
    }
  }
  extimes.sortUnique();

  int st = 0;
  for ( i = 0, end = extimes.count();  i < end;  ++i ) {
    int j = times.removeSorted( extimes[i], st );
    if ( j >= 0 )
      st = j;
  }
  return times;
}

DateTimeList Recurrence::timesInInterval( const KDateTime &start, const KDateTime &end ) const
{
  int i, count;
  DateTimeList times;
  for ( i = 0, count = d->mRRules.count();  i < count;  ++i ) {
    times += d->mRRules[i]->timesInInterval( start, end );
  }
  times += d->mRDateTimes;
  KDateTime kdt( startDateTime() );
  for ( i = 0, count = d->mRDates.count();  i < count;  ++i ) {
    kdt.setDate( d->mRDates[i] );
    times += kdt;
  }
  times.sortUnique();

  // Remove excluded times
  int idt = 0;
  int enddt = times.count();
  for ( i = 0, count = d->mExDates.count();  i < count && idt < enddt;  ++i ) {
    while ( idt < enddt && times[idt].date() < d->mExDates[i] ) ++idt;
    while ( idt < enddt && times[idt].date() == d->mExDates[i] ) {
      times.removeAt(idt);
      --enddt;
    }
  }
  DateTimeList extimes;
  for ( i = 0, count = d->mExRules.count();  i < count;  ++i ) {
    extimes += d->mExRules[i]->timesInInterval( start, end );
  }
  extimes += d->mExDateTimes;
  extimes.sortUnique();

  int st = 0;
  for ( i = 0, count = extimes.count();  i < count;  ++i ) {
    int j = times.removeSorted( extimes[i], st );
    if ( j >= 0 )
      st = j;
  }

  return times;
}

KDateTime Recurrence::getNextDateTime( const KDateTime &preDateTime ) const
{
kDebug(5800) << " Recurrence::getNextDateTime after " << preDateTime.dateTime() << endl;
  KDateTime nextDT = preDateTime;
  // prevent infinite loops, e.g. when an exrule extinguishes an rrule (e.g.
  // the exrule is identical to the rrule). If an occurrence is found, break
  // out of the loop by returning that KDateTime
// TODO_Recurrence: Is a loop counter of 1000 really okay? I mean for secondly
// recurrence, an exdate might exclude more than 1000 intervals!
  int loop = 0;
  while ( loop < 1000 ) {
    // Outline of the algo:
    //   1) Find the next date/time after preDateTime when the event could recur
    //     1.0) Add the start date if it's after preDateTime
    //     1.1) Use the next occurrence from the explicit RDATE lists
    //     1.2) Add the next recurrence for each of the RRULEs
    //   2) Take the earliest recurrence of these = KDateTime nextDT
    //   3) If that date/time is not excluded, either explicitly by an EXDATE or
    //      by an EXRULE, return nextDT as the next date/time of the recurrence
    //   4) If it's excluded, start all at 1), but starting at nextDT (instead
    //      of preDateTime). Loop at most 1000 times.
    ++loop;
    // First, get the next recurrence from the RDate lists
    DateTimeList dates;
    if ( nextDT < startDateTime() ) dates << startDateTime();
    int end;
    // Assume that the rdatetime list is sorted
    int i = d->mRDateTimes.findGT( nextDT );
    if ( i >= 0 ) {
      dates << d->mRDateTimes[i];
    }

/*kDebug(5800) << "    nextDT: " << nextDT << ", startDT: " << startDateTime() << endl;
kDebug(5800) << "   getNextDateTime: found " << dates.count() << " RDATES and DTSTART in loop " << loop << endl;*/
    KDateTime kdt( startDateTime() );
    for ( i = 0, end = d->mRDates.count();  i < end;  ++i ) {
      kdt.setDate( d->mRDates[i] );
      if ( kdt > nextDT ) {
        dates << kdt;
        break;
      }
    }

    // Add the next occurrences from all RRULEs.
    for ( i = 0, end = d->mRRules.count();  i < end;  ++i ) {
      KDateTime dt = d->mRRules[i]->getNextDate( nextDT );
      if ( dt.isValid() ) dates << dt;
    }

    // Take the first of these (all others can't be used later on)
    dates.sortUnique();
// kDebug(5800) << "   getNextDateTime: found " << dates.count() << " dates in loop " << loop << endl;

    if ( dates.isEmpty() ) return KDateTime();
    nextDT = dates.first();

    // Check if that date/time is excluded explicitly or by an exrule:
    if ( !d->mExDates.containsSorted( nextDT.date() ) && !d->mExDateTimes.containsSorted( nextDT ) ) {
// kDebug(5800) << "   NextDT" << nextDT << " not excluded by EXDATE " << endl;
      bool allowed = true;
      for ( i = 0, end = d->mExRules.count();  i < end;  ++i ) {
        allowed = allowed && !( d->mExRules[i]->recursAt( nextDT ) );
      }
// kDebug(5800) << "   NextDT " << nextDT << ", allowed=" << allowed << endl;
      if ( allowed ) return nextDT;
    }
  }

  // Couldn't find a valid occurrences in 1000 loops, something is wrong!
  return KDateTime();
}

KDateTime Recurrence::getPreviousDateTime( const KDateTime &afterDateTime ) const
{
  KDateTime prevDT = afterDateTime;
  // prevent infinite loops, e.g. when an exrule extinguishes an rrule (e.g.
  // the exrule is identical to the rrule). If an occurrence is found, break
  // out of the loop by returning that KDateTime
  int loop = 0;
  while ( loop < 1000 ) {
    // Outline of the algo:
    //   1) Find the next date/time after preDateTime when the event could recur
    //     1.1) Use the next occurrence from the explicit RDATE lists
    //     1.2) Add the next recurrence for each of the RRULEs
    //   2) Take the earliest recurrence of these = KDateTime nextDT
    //   3) If that date/time is not excluded, either explicitly by an EXDATE or
    //      by an EXRULE, return nextDT as the next date/time of the recurrence
    //   4) If it's excluded, start all at 1), but starting at nextDT (instead
    //      of preDateTime). Loop at most 1000 times.
    ++loop;
    // First, get the next recurrence from the RDate lists
    DateTimeList dates;
    if ( prevDT > startDateTime() ) dates << startDateTime();

    int i = d->mRDateTimes.findLT( prevDT );
    if ( i >= 0 ) {
      dates << d->mRDateTimes[i];
    }

    KDateTime kdt( startDateTime() );
    for ( i = d->mRDates.count();  --i >= 0;  ) {
      kdt.setDate( d->mRDates[i] );
      if ( kdt < prevDT ) {
        dates << kdt;
        break;
      }
    }

    // Add the previous occurrences from all RRULEs.
    int end;
    for ( i = 0, end = d->mRRules.count();  i < end;  ++i ) {
      KDateTime dt = d->mRRules[i]->getPreviousDate( prevDT );
      if ( dt.isValid() ) dates << dt;
    }
kDebug(5800) << "   getPreviousDateTime: found " << dates.count() << " dates in loop " << loop << endl;

    // Take the last of these (all others can't be used later on)
    dates.sortUnique();
    if ( dates.isEmpty() ) return KDateTime();
    prevDT = dates.last();

    // Check if that date/time is excluded explicitly or by an exrule:
    if ( !d->mExDates.containsSorted( prevDT.date() ) && !d->mExDateTimes.containsSorted( prevDT ) ) {
      bool allowed = true;
      for ( i = 0, end = d->mExRules.count();  i < end;  ++i ) {
        allowed = allowed && !( d->mExRules[i]->recursAt( prevDT ) );
      }
      if ( allowed ) return prevDT;
    }
  }

  // Couldn't find a valid occurrences in 1000 loops, something is wrong!
  return KDateTime();
}


/***************************** PROTECTED FUNCTIONS ***************************/


RecurrenceRule::List Recurrence::rRules() const
{
  return d->mRRules;
}

void Recurrence::addRRule( RecurrenceRule *rrule )
{
  if ( d->mRecurReadOnly || !rrule ) return;
  rrule->setFloats( d->mFloating );
  d->mRRules.append( rrule );
  rrule->addObserver( this );
  updated();
}

void Recurrence::removeRRule( RecurrenceRule *rrule )
{
  if (d->mRecurReadOnly) return;
  d->mRRules.removeAll( rrule );
  rrule->removeObserver( this );
  updated();
}

RecurrenceRule::List Recurrence::exRules() const
{
  return d->mExRules;
}

void Recurrence::addExRule( RecurrenceRule *exrule )
{
  if ( d->mRecurReadOnly || !exrule ) return;
  exrule->setFloats( d->mFloating );
  d->mExRules.append( exrule );
  exrule->addObserver( this );
  updated();
}

void Recurrence::removeExRule( RecurrenceRule *exrule )
{
  if (d->mRecurReadOnly) return;
  d->mExRules.removeAll( exrule );
  exrule->removeObserver( this );
  updated();
}


DateTimeList Recurrence::rDateTimes() const
{
  return d->mRDateTimes;
}

void Recurrence::setRDateTimes( const DateTimeList &rdates )
{
  if ( d->mRecurReadOnly ) return;
  d->mRDateTimes = rdates;
  d->mRDateTimes.sortUnique();
  updated();
}

void Recurrence::addRDateTime( const KDateTime &rdate )
{
  if ( d->mRecurReadOnly ) return;
  d->mRDateTimes.insertSorted( rdate );
  updated();
}


DateList Recurrence::rDates() const
{
  return d->mRDates;
}

void Recurrence::setRDates( const DateList &rdates )
{
  if ( d->mRecurReadOnly ) return;
  d->mRDates = rdates;
  d->mRDates.sortUnique();
  updated();
}

void Recurrence::addRDate( const QDate &rdate )
{
  if ( d->mRecurReadOnly ) return;
  d->mRDates.insertSorted( rdate );
  updated();
}


DateTimeList Recurrence::exDateTimes() const
{
  return d->mExDateTimes;
}

void Recurrence::setExDateTimes( const DateTimeList &exdates )
{
  if ( d->mRecurReadOnly ) return;
  d->mExDateTimes = exdates;
  d->mExDateTimes.sortUnique();
}

void Recurrence::addExDateTime( const KDateTime &exdate )
{
  if ( d->mRecurReadOnly ) return;
  d->mExDateTimes.insertSorted( exdate );
  updated();
}


DateList Recurrence::exDates() const
{
  return d->mExDates;
}

void Recurrence::setExDates( const DateList &exdates )
{
  if ( d->mRecurReadOnly ) return;
  d->mExDates = exdates;
  d->mExDates.sortUnique();
  updated();
}

void Recurrence::addExDate( const QDate &exdate )
{
  if ( d->mRecurReadOnly ) return;
  d->mExDates.insertSorted( exdate );
  updated();
}

void Recurrence::recurrenceChanged( RecurrenceRule * )
{
  updated();
}


// %%%%%%%%%%%%%%%%%% end:Recurrencerule %%%%%%%%%%%%%%%%%%

void Recurrence::dump() const
{
  kDebug(5800) << "Recurrence::dump():" << endl;

  int i;
  int count = d->mRRules.count();
  kDebug(5800) << "  -) " << count << " RRULEs: " << endl;
  for ( i = 0;  i < count;  ++i ) {
    kDebug(5800) << "    -) RecurrenceRule : " << endl;
    d->mRRules[i]->dump();
  }
  count = d->mExRules.count();
  kDebug(5800) << "  -) " << count << " EXRULEs: " << endl;
  for ( i = 0;  i < count;  ++i ) {
    kDebug(5800) << "    -) ExceptionRule : " << endl;
    d->mExRules[i]->dump();
  }


  count = d->mRDates.count();
  kDebug(5800) << endl << "  -) " << count << " Recurrence Dates: " << endl;
  for ( i = 0;  i < count;  ++i ) {
    kDebug(5800) << "     " << d->mRDates[i] << endl;
  }
  count = d->mRDateTimes.count();
  kDebug(5800) << endl << "  -) " << count << " Recurrence Date/Times: " << endl;
  for ( i = 0;  i < count;  ++i ) {
    kDebug(5800) << "     " << d->mRDateTimes[i].dateTime() << endl;
  }
  count = d->mExDates.count();
  kDebug(5800) << endl << "  -) " << count << " Exceptions Dates: " << endl;
  for ( i = 0;  i < count;  ++i ) {
    kDebug(5800) << "     " << d->mExDates[i] << endl;
  }
  count = d->mExDateTimes.count();
  kDebug(5800) << endl << "  -) " << count << " Exception Date/Times: " << endl;
  for ( i = 0;  i < count;  ++i ) {
    kDebug(5800) << "     " << d->mExDateTimes[i].dateTime() << endl;
  }
}
