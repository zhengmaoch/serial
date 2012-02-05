/* Copyright 2012 William Woodall and John Harrison */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <paths.h>
#include <sysexits.h>
#include <termios.h>
#include <sys/param.h>
#include <pthread.h>

#if defined(__linux__)
#include <linux/serial.h>
#endif

#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#include "serial/impl/unix.h"

#ifndef TIOCINQ
#ifdef FIONREAD
#define TIOCINQ FIONREAD
#else
#define TIOCINQ 0x541B
#endif
#endif

using std::string;
using std::invalid_argument;
using serial::Serial;
using serial::SerialExecption;
using serial::PortNotOpenedException;
using serial::IOException;


Serial::SerialImpl::SerialImpl (const string &port, unsigned long baudrate,
                                long timeout, bytesize_t bytesize,
                                parity_t parity, stopbits_t stopbits,
                                flowcontrol_t flowcontrol)
: port_ (port), fd_ (-1), is_open_ (false), xonxoff_ (true), rtscts_ (false),
  timeout_ (timeout), baudrate_ (baudrate), parity_ (parity),
  bytesize_ (bytesize), stopbits_ (stopbits), flowcontrol_ (flowcontrol)
{
  pthread_mutex_init(&this->read_mutex, NULL);
  pthread_mutex_init(&this->write_mutex, NULL);
  if (port_.empty () == false)
    open ();
}

Serial::SerialImpl::~SerialImpl ()
{
  close();
  pthread_mutex_destroy(&this->read_mutex);
  pthread_mutex_destroy(&this->write_mutex);
}

void
Serial::SerialImpl::open ()
{
  if (port_.empty ())
  {
    throw invalid_argument ("Empty port is invalid.");
  }
  if (is_open_ == true)
  {
    throw SerialExecption ("Serial port already open.");
  }

  fd_ = ::open (port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);

  if (fd_ == -1)
  {
    switch (errno)
    {
      case EINTR:
        // Recurse because this is a recoverable error.
        open ();
        return;
      case ENFILE:
      case EMFILE:
        throw IOException ("Too many file handles open.");
        break;
      default:
        throw IOException (errno);
    }
  }

  reconfigurePort();
  is_open_ = true;
}

void
Serial::SerialImpl::reconfigurePort ()
{
  if (fd_ == -1)
  {
    // Can only operate on a valid file descriptor
    throw IOException ("Invalid file descriptor, is the serial port open?");
  }

  struct termios options; // The options for the file descriptor

  if (tcgetattr(fd_, &options) == -1)
  {
    throw IOException ("::tcgetattr");
  }

  // set up raw mode / no echo / binary
  options.c_cflag |= (unsigned long)  (CLOCAL|CREAD);
  options.c_lflag &= (unsigned long) ~(ICANON|ECHO|ECHOE|ECHOK|ECHONL|
                                       ISIG|IEXTEN); //|ECHOPRT

  options.c_oflag &= (unsigned long) ~(OPOST);
  options.c_iflag &= (unsigned long) ~(INLCR|IGNCR|ICRNL|IGNBRK);
#ifdef IUCLC
  options.c_iflag &= (unsigned long) ~IUCLC;
#endif
#ifdef PARMRK
  options.c_iflag &= (unsigned long) ~PARMRK;
#endif

  // setup baud rate
  bool custom_baud = false;
  speed_t baud;
  switch (baudrate_)
  {
#ifdef B0
    case 0: baud = B0; break;
#endif
#ifdef B50
    case 50: baud = B50; break;
#endif
#ifdef B75
    case 75: baud = B75; break;
#endif
#ifdef B110
    case 110: baud = B110; break;
#endif
#ifdef B134
    case 134: baud = B134; break;
#endif
#ifdef B150
    case 150: baud = B150; break;
#endif
#ifdef B200
    case 200: baud = B200; break;
#endif
#ifdef B300
    case 300: baud = B300; break;
#endif
#ifdef B600
    case 600: baud = B600; break;
#endif
#ifdef B1200
    case 1200: baud = B1200; break;
#endif
#ifdef B1800
    case 1800: baud = B1800; break;
#endif
#ifdef B2400
    case 2400: baud = B2400; break;
#endif
#ifdef B4800
    case 4800: baud = B4800; break;
#endif
#ifdef B7200
    case 7200: baud = B7200; break;
#endif
#ifdef B9600
    case 9600: baud = B9600; break;
#endif
#ifdef B14400
    case 14400: baud = B14400; break;
#endif
#ifdef B19200
    case 19200: baud = B19200; break;
#endif
#ifdef B28800
    case 28800: baud = B28800; break;
#endif
#ifdef B57600
    case 57600: baud = B57600; break;
#endif
#ifdef B76800
    case 76800: baud = B76800; break;
#endif
#ifdef B38400
    case 38400: baud = B38400; break;
#endif
#ifdef B115200
    case 115200: baud = B115200; break;
#endif
#ifdef B128000
    case 128000: baud = B128000; break;
#endif
#ifdef B153600
    case 153600: baud = B153600; break;
#endif
#ifdef B230400
    case 230400: baud = B230400; break;
#endif
#ifdef B256000
    case 256000: baud = B256000; break;
#endif
#ifdef B460800
    case 460800: baud = B460800; break;
#endif
#ifdef B921600
    case 921600: baud = B921600; break;
#endif
    default:
      custom_baud = true;
// Mac OS X 10.x Support
#if defined(__APPLE__) && defined(__MACH__)
#define IOSSIOSPEED _IOW('T', 2, speed_t)
      int new_baud = static_cast<int> (baudrate_);
      if (ioctl (fd_, IOSSIOSPEED, &new_baud, 1) < 0)
      {
        throw IOException (errno);
      }
// Linux Support
#elif defined(__linux__)
      struct serial_struct ser;
      ioctl(fd_, TIOCGSERIAL, &ser);
      // set custom divisor
      ser.custom_divisor = ser.baud_base / baudrate_;
      // update flags
      ser.flags &= ~ASYNC_SPD_MASK;
      ser.flags |= ASYNC_SPD_CUST;

      if (ioctl(fd_, TIOCSSERIAL, ser) < 0)
      {
        throw IOException (errno);
      }
#else
      throw invalid_argument ("OS does not currently support custom bauds");
#endif
  }
  if (custom_baud == false)
  {
#ifdef _BSD_SOURCE
    ::cfsetspeed(&options, baud);
#else
    ::cfsetispeed(&options, baud);
    ::cfsetospeed(&options, baud);
#endif
  }

  // setup char len
  options.c_cflag &= (unsigned long) ~CSIZE;
  if (bytesize_ == EIGHTBITS)
      options.c_cflag |= CS8;
  else if (bytesize_ == SEVENBITS)
      options.c_cflag |= CS7;
  else if (bytesize_ == SIXBITS)
      options.c_cflag |= CS6;
  else if (bytesize_ == FIVEBITS)
      options.c_cflag |= CS5;
  else
      throw invalid_argument ("invalid char len");
  // setup stopbits
  if (stopbits_ == STOPBITS_ONE)
      options.c_cflag &= (unsigned long) ~(CSTOPB);
  else if (stopbits_ == STOPBITS_ONE_POINT_FIVE)
  // ONE POINT FIVE same as TWO.. there is no POSIX support for 1.5
      options.c_cflag |=  (CSTOPB);
  else if (stopbits_ == STOPBITS_TWO)
      options.c_cflag |=  (CSTOPB);
  else
      throw invalid_argument ("invalid stop bit");
  // setup parity
  options.c_iflag &= (unsigned long) ~(INPCK|ISTRIP);
  if (parity_ == PARITY_NONE)
  {
    options.c_cflag &= (unsigned long) ~(PARENB|PARODD);
  }
  else if (parity_ == PARITY_EVEN)
  {
    options.c_cflag &= (unsigned long) ~(PARODD);
    options.c_cflag |=  (PARENB);
  }
  else if (parity_ == PARITY_ODD)
  {
    options.c_cflag |=  (PARENB|PARODD);
  }
  else
  {
    throw invalid_argument ("invalid parity");
  }
  // setup flow control
  // xonxoff
#ifdef IXANY
  if (xonxoff_)
    options.c_iflag |=  (IXON|IXOFF); //|IXANY)
  else
    options.c_iflag &= (unsigned long) ~(IXON|IXOFF|IXANY);
#else
  if (xonxoff_)
    options.c_iflag |=  (IXON|IXOFF);
  else
    options.c_iflag &= (unsigned long) ~(IXON|IXOFF);
#endif
  // rtscts
#ifdef CRTSCTS
  if (rtscts_)
    options.c_cflag |=  (CRTSCTS);
  else
    options.c_cflag &= (unsigned long) ~(CRTSCTS);
#elif defined CNEW_RTSCTS
  if (rtscts_)
    options.c_cflag |=  (CNEW_RTSCTS);
  else
    options.c_cflag &= (unsigned long) ~(CNEW_RTSCTS);
#else
#error "OS Support seems wrong."
#endif

  // http://www.unixwiz.net/techtips/termios-vmin-vtime.html
  // this basically sets the read call up to be a polling read, 
  // but we are using select to ensure there is data available 
  // to read before each call, so we should never needlessly poll
  options.c_cc[VMIN] = 0;
  options.c_cc[VTIME] = 0;

  // activate settings
  ::tcsetattr (fd_, TCSANOW, &options);
}

void
Serial::SerialImpl::close ()
{
  if (is_open_ == true)
  {
    if (fd_ != -1)
    {
      ::close (fd_); // Ignoring the outcome
      fd_ = -1;
    }
    is_open_ = false;
  }
}

bool
Serial::SerialImpl::isOpen () const
{
  return is_open_;
}

size_t
Serial::SerialImpl::available ()
{
  if (!is_open_)
  {
    return 0;
  }
  int count = 0;
  int result = ioctl (fd_, TIOCINQ, &count);
  if (result == 0)
  {
    return static_cast<size_t> (count);
  }
  else
  {
    throw IOException (errno);
  }
}

inline void get_time_now(struct timespec &time) {
# ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
  clock_serv_t cclock;
  mach_timespec_t mts;
  host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
  clock_get_time(cclock, &mts);
  mach_port_deallocate(mach_task_self(), cclock);
  time.tv_sec = mts.tv_sec;
  time.tv_nsec = mts.tv_nsec;
# else
  clock_gettime(CLOCK_REALTIME, &time);
# endif
}

size_t
Serial::SerialImpl::read (unsigned char* buf, size_t size)
{
  if (!is_open_)
  {
    throw PortNotOpenedException ("Serial::read");
  }
  fd_set readfds;
  size_t bytes_read = 0;
  struct timeval timeout;
  timeout.tv_sec =                    timeout_ / 1000;
  timeout.tv_usec = static_cast<int> (timeout_ % 1000) * 1000;
  while (bytes_read < size)
  {
    FD_ZERO (&readfds);
    FD_SET (fd_, &readfds);
    // On Linux the timeout struct is updated by select to contain the time 
    // left on the timeout to make looping easier, but on other platforms this 
    // does not occur.
#if !defined(__linux__)
    // Begin timing select
    struct timespec start, end;
    get_time_now(start);
#endif
    // Do the select
    int r = select (fd_ + 1, &readfds, NULL, NULL, &timeout);
#if !defined(__linux__)
    // Calculate difference and update the structure
    get_time_now(end);
    // Calculate the time select took
    struct timeval diff;
    diff.tv_sec = end.tv_sec-start.tv_sec;
    diff.tv_usec = (end.tv_nsec-start.tv_nsec)/1000;
    // Update the timeout
    if (timeout.tv_sec <= diff.tv_sec) {
      timeout.tv_sec = 0;
    } else {
      timeout.tv_sec -= diff.tv_sec;
    }
    if (timeout.tv_usec <= diff.tv_usec) {
      timeout.tv_usec = 0;
    } else {
      timeout.tv_usec -= diff.tv_usec;
    }
#endif

    // Figure out what happened by looking at select's response 'r'
/** Error **/
    if (r < 0) {
      // Select was interrupted, try again
      if (errno == EINTR) {
        continue;
      }
      // Otherwise there was some error
      throw IOException (errno);
    }
/** Timeout **/
    if (r == 0) {
      break;
    }
/** Something ready to read **/
    if (r > 0) {
      // Make sure our file descriptor is in the ready to read list
      if (FD_ISSET (fd_, &readfds)) {
        // This should be non-blocking returning only what is avaialble now
        //  Then returning so that select can block again.
        ssize_t bytes_read_now = ::read (fd_, buf, size-bytes_read);
        // read should always return some data as select reported it was
        // ready to read when we get to this point.
        if (bytes_read_now < 1)
        {
          // Disconnected devices, at least on Linux, show the
          // behavior that they are always ready to read immediately
          // but reading returns nothing.
          throw SerialExecption ("device reports readiness to read but "
                                 "returned no data (device disconnected?)");
        }
        // Update bytes_read
        bytes_read += static_cast<size_t> (bytes_read_now);
        // If bytes_read == size then we have read everything we need
        if (bytes_read == size) {
          break;
        }
        // If bytes_read < size then we have more to read
        if (bytes_read < size) {
          continue;
        }
        // If bytes_read > size then we have over read, which shouldn't happen
        if (bytes_read > size) {
          throw SerialExecption ("read over read, too many bytes where "
                                 "read, this shouldn't happen, might be "
                                 "a logical error!");
        }
      }
      // This shouldn't happen, if r > 0 our fd has to be in the list!
      throw IOException ("select reports ready to read, but our fd isn't"
                         " in the list, this shouldn't happen!");
    }
  }
  return bytes_read;
}

size_t
Serial::SerialImpl::write (const string &data)
{
  if (is_open_ == false)
  {
    throw PortNotOpenedException ("Serial::write");
  }

  return static_cast<size_t> (::write (fd_, data.c_str (), data.length ()));
}

void
Serial::SerialImpl::setPort (const string &port)
{
  port_ = port;
}

string
Serial::SerialImpl::getPort () const
{
  return port_;
}

void
Serial::SerialImpl::setTimeout (long timeout)
{
  timeout_ = timeout;
}

long
Serial::SerialImpl::getTimeout () const
{
  return timeout_;
}

void
Serial::SerialImpl::setBaudrate (unsigned long baudrate)
{
  baudrate_ = baudrate;
  if (is_open_)
    reconfigurePort ();
}

unsigned long
Serial::SerialImpl::getBaudrate () const
{
  return baudrate_;
}

void
Serial::SerialImpl::setBytesize (serial::bytesize_t bytesize)
{
  bytesize_ = bytesize;
  if (is_open_)
    reconfigurePort ();
}

serial::bytesize_t
Serial::SerialImpl::getBytesize () const
{
  return bytesize_;
}

void
Serial::SerialImpl::setParity (serial::parity_t parity)
{
  parity_ = parity;
  if (is_open_)
    reconfigurePort ();
}

serial::parity_t
Serial::SerialImpl::getParity () const
{
  return parity_;
}

void
Serial::SerialImpl::setStopbits (serial::stopbits_t stopbits)
{
  stopbits_ = stopbits;
  if (is_open_)
    reconfigurePort ();
}

serial::stopbits_t
Serial::SerialImpl::getStopbits () const
{
  return stopbits_;
}

void
Serial::SerialImpl::setFlowcontrol (serial::flowcontrol_t flowcontrol)
{
  flowcontrol_ = flowcontrol;
  if (is_open_)
    reconfigurePort ();
}

serial::flowcontrol_t
Serial::SerialImpl::getFlowcontrol () const
{
  return flowcontrol_;
}

void
Serial::SerialImpl::flush ()
{
  if (is_open_ == false)
  {
    throw PortNotOpenedException ("Serial::flush");
  }
  tcdrain (fd_);
}

void
Serial::SerialImpl::flushInput ()
{
  if (is_open_ == false)
  {
    throw PortNotOpenedException ("Serial::flushInput");
  }
  tcflush (fd_, TCIFLUSH);
}

void
Serial::SerialImpl::flushOutput ()
{
  if (is_open_ == false)
  {
    throw PortNotOpenedException ("Serial::flushOutput");
  }
  tcflush (fd_, TCOFLUSH);
}

void
Serial::SerialImpl::sendBreak (int duration)
{
  if (is_open_ == false)
  {
    throw PortNotOpenedException ("Serial::sendBreak");
  }
  tcsendbreak (fd_, static_cast<int> (duration/4));
}

void
Serial::SerialImpl::setBreak (bool level)
{
  if (is_open_ == false)
  {
    throw PortNotOpenedException ("Serial::setBreak");
  }
  if (level)
  {
    ioctl (fd_, TIOCSBRK);
  }
  else {
    ioctl (fd_, TIOCCBRK);
  }
}

void
Serial::SerialImpl::setRTS (bool level)
{
  if (is_open_ == false)
  {
    throw PortNotOpenedException ("Serial::setRTS");
  }
  if (level)
  {
    ioctl (fd_, TIOCMBIS, TIOCM_RTS);
  }
  else {
    ioctl (fd_, TIOCMBIC, TIOCM_RTS);
  }
}

void
Serial::SerialImpl::setDTR (bool level)
{
  if (is_open_ == false)
  {
    throw PortNotOpenedException ("Serial::setDTR");
  }
  if (level)
  {
    ioctl (fd_, TIOCMBIS, TIOCM_DTR);
  }
  else
  {
    ioctl (fd_, TIOCMBIC, TIOCM_DTR);
  }
}

bool
Serial::SerialImpl::getCTS ()
{
  if (is_open_ == false)
  {
    throw PortNotOpenedException ("Serial::getCTS");
  }
  int s = ioctl (fd_, TIOCMGET, 0);
  return (s & TIOCM_CTS) != 0;
}

bool
Serial::SerialImpl::getDSR()
{
  if (is_open_ == false)
  {
    throw PortNotOpenedException ("Serial::getDSR");
  }
  int s = ioctl(fd_, TIOCMGET, 0);
  return (s & TIOCM_DSR) != 0;
}

bool
Serial::SerialImpl::getRI()
{
  if (is_open_ == false)
  {
    throw PortNotOpenedException ("Serial::getRI");
  }
  int s = ioctl (fd_, TIOCMGET, 0);
  return (s & TIOCM_RI) != 0;
}

bool
Serial::SerialImpl::getCD()
{
  if (is_open_ == false)
  {
    throw PortNotOpenedException ("Serial::getCD");
  }
  int s = ioctl (fd_, TIOCMGET, 0);
  return (s & TIOCM_CD) != 0;
}

void
Serial::SerialImpl::readLock() {
  int result = pthread_mutex_lock(&this->read_mutex);
  if (result) {
    throw (IOException (result));
  }
}

void
Serial::SerialImpl::readUnlock() {
  int result = pthread_mutex_unlock(&this->read_mutex);
  if (result) {
    throw (IOException (result));
  }
}

void
Serial::SerialImpl::writeLock() {
  int result = pthread_mutex_lock(&this->write_mutex);
  if (result) {
    throw (IOException (result));
  }
}

void
Serial::SerialImpl::writeUnlock() {
  int result = pthread_mutex_unlock(&this->write_mutex);
  if (result) {
    throw (IOException (result));
  }
}
