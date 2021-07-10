struct TTime {
  public:
    TTime() {
      clear();
    }
    TTime(byte hours, byte minutes) {
      _hour = hours; 
      _minutes = minutes;
    }
    TTime(byte minutes) {
      _hour = minutes / 60;
      _minutes = minutes % 60;
      _seconds = 0;      
    }
    void clear() {
      _hour = 99;
      _minutes = 99;
      _seconds = 99;
    }
    byte hour() const {
      return _hour;
    }
    void hour(byte h) {
      _hour = h;
    }
    byte minutes() const {
      return _minutes;
    }
    void minutes(byte m) {
      _minutes = m;
    }
    byte seconds() const {
      return _seconds;
    }
    void seconds(byte s) {
      _seconds = s;
    }
    String format(bool withSeconds = false) {
      String s = "";
      if (_hour <= 9)
        s = "0";
      s += String(_hour);
      s += ":";
      if (_minutes <= 9)
        s += "0";
      s += String(_minutes);
      if (withSeconds) {
        s += ":";
        if (_seconds <= 9)
          s += "0";
        s += String(_seconds);
      }
      return s;
    }
    int toMinutes() {
      return _hour * 60 + _minutes;
    }
    long toSeconds() {
      long s = (long)toMinutes() * 60 + _seconds;
      return s; 
    }
    inline bool operator==(TTime t) {
      if ((t._hour == _hour) &&
        (t._minutes = _minutes) &&
        (t.seconds == _seconds))
        return true;
        else
        return false;
    }

  private:
    byte _hour;
    byte _minutes;
    byte _seconds;
    void validteNotNull() {
      return ((_hour != 99) && (_minutes != 99) && (_seconds != 99));
    }
};
