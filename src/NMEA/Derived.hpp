// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Geo/GeoPoint.hpp"
#include "Geo/SpeedVector.hpp"
#include "Task/Stats/TaskStats.hpp"
#include "Task/Stats/CommonStats.hpp"
#include "Contest/ContestStatistics.hpp"
#include "FlyingState.hpp"
#include "NMEA/VarioInfo.hpp"
#include "NMEA/ClimbInfo.hpp"
#include "NMEA/CirclingInfo.hpp"
#include "Engine/ThermalBand/ThermalEncounterBand.hpp"
#include "Engine/ThermalBand/ThermalEncounterCollection.hpp"
#include "NMEA/ThermalLocator.hpp"
#include "NMEA/Validity.hpp"
#include "NMEA/ClimbHistory.hpp"
#include "TeamCode/TeamCode.hpp"
#include "Engine/Navigation/TraceHistory.hpp"
#include "time/BrokenDateTime.hpp"
#include "Engine/GlideSolvers/GlidePolar.hpp"
#include "Atmosphere/Pressure.hpp"
#include "Engine/Route/Route.hpp"
#include "Computer/WaveResult.hpp"

#include <type_traits>

/** Derived terrain altitude information, including glide range */
struct TerrainInfo
{
  /** True if terrain is valid, False otherwise */
  bool terrain_valid;

  /** Does the attribute #TerrainBase have a valid value? */
  bool terrain_base_valid;

  /** Does the attribute #AltitudeAGL have a valid value? */
  bool altitude_agl_valid;

  /** Terrain altitude */
  double terrain_altitude;

  /** Lowest height within glide range */
  double terrain_base;

  /** Altitude over terrain */
  double altitude_agl;

  /**
   * Location of terrain warning.
   *
   * Check GeoPoint::IsValid().
   */
  GeoPoint terrain_warning_location;

  void Clear();

  /**
   * Returns the terrain base, and falls back for terrain altitude if
   * the base is not known.
   */
  double GetTerrainBaseFallback() const {
    return terrain_base_valid
      ? terrain_base
      : terrain_altitude;
  }
};

static_assert(std::is_trivial<TerrainInfo>::value, "type is not trivial");

/** Derived team code information */
struct TeamInfo
{
  /** Are #teammate_vector and #TeammateLocation available? */
  bool teammate_available;

  /** is #flarm_teammate_code current or did we lose him? */
  bool flarm_teammate_code_current;

  /** Team code */
  TeamCode own_teammate_code;

  /** Vector to the chosen team mate */
  GeoVector teammate_vector;

  /** Position of the chosen team mate */
  GeoPoint teammate_location;

  /**
   * The team code of the FLARM teammate.  Check TeamCode::IsDefined()
   * before using this attribute.
   */
  TeamCode flarm_teammate_code;

  void Clear();
};

static_assert(std::is_trivial<TeamInfo>::value, "type is not trivial");

struct AirspaceWarningsInfo {
  /**
   * The time stamp of the most recent airspace warning.  Check if
   * this value gets increased to see if there's a new warning.
   */
  Validity latest;

  void Clear();
};

static_assert(std::is_trivial<AirspaceWarningsInfo>::value, "type is not trivial");

/**
 * A struct that holds all the calculated values derived from the data in the
 * NMEA_INFO struct
 */
struct DerivedInfo: 
  public VarioInfo,
  public ClimbInfo,
  public CirclingInfo,
  public TerrainInfo,
  public TeamInfo
{
  /**
   * GPS date and time (local).
   *
   * Check IsPlausible(), IsDatePlausible() or IsTimePlausible()
   * before using this attribute.
   */
  BrokenDateTime date_time_local;

  /** Speed to fly block/dolphin (m/s) */
  double V_stf;

  /** Speed to fly maximum efficiency (m/s) */
  double V_max_eff;

  /** Auto QNH calculation result. */
  AtmosphericPressure pressure;
  Validity pressure_available;

  ClimbHistory climb_history;

  WaveResult wave;

  /** Does #estimated_wind have a meaningful value? */
  Validity estimated_wind_available;

  /** Wind speed, direction */
  SpeedVector estimated_wind;

  /** Is the wind available? */
  Validity wind_available;

  /**
   * The effective wind vector; depending on the settings, this is
   * either ExternalWind, calculated wind or manual wind.
   */
  SpeedVector wind;

  /**
   * Where did we obtain the effective wind vector?
   */
  enum class WindSource : uint8_t {
    /**
     * No wind vector available.  This should be kept in sync with
     * #wind_available.
     */
    NONE,

    /**
     * The user has entered a wind vector manually.
     */
    MANUAL,

    /**
     * Calculated by #CirclingWind.
     */
    CIRCLING,

    /**
     * Calculated by #WindEKF.
     */
    EKF,

    /**
     * The wind vector was received from an external device.
     */
    EXTERNAL,
  } wind_source;

  Validity head_wind_available;
  double head_wind;

  /** Distance to zoom to for autozoom */
  double auto_zoom_distance;

  Validity sun_data_available;
  /** Sun's azimuth at the current location and time */
  Angle sun_azimuth;

  /** Copy of task statistics data for active task */
  TaskStats task_stats;

  /** Copy of task statistics data for ordered task */
  TaskStats ordered_task_stats;

  /** Copy of common task statistics data */
  CommonStats common_stats;
  /** Copy of contest statistics data */
  ContestStatistics contest_stats;

  FlyingState flight;

  ThermalEncounterBand thermal_encounter_band;
  ThermalEncounterCollection thermal_encounter_collection;

  ThermalLocatorInfo thermal_locator;

  /** Store of short term history of variables */
  TraceHistory trace_history;

  Validity auto_mac_cready_available;
  double auto_mac_cready;

  /** Glide polar used for safety calculations */
  GlidePolar glide_polar_safety;

  AirspaceWarningsInfo airspace_warnings;

  /** Route plan for current leg avoiding airspace */
  StaticRoute planned_route;

  /**
   * Thermal value of next leg that is equivalent (gives the same average
   * speed) to the current MacCready setting. A negative value should be
   * treated as invalid.
   */
  double next_leg_eq_thermal;

  int fuel_burn_time_remain;
  Validity fuel_burn_time_remain_available;

  /**
   * @todo Reset to cleared state
   */
  void Reset();

  void Expire(TimeStamp time) noexcept;

  /**
   * Return the current wind vector, or the null vector if no wind is
   * available.
   */
  [[gnu::pure]]
  SpeedVector GetWindOrZero() const {
    return wind_available
      ? wind
      : SpeedVector::Zero();
  }

  void ProvideAutoMacCready(TimeStamp clock, double mc) {
    if (auto_mac_cready_available &&
        fabs(auto_mac_cready - mc) < 0.05)
      /* change is too small, ignore the new value to limit the rate */
      return;

    auto_mac_cready = mc;
    auto_mac_cready_available.Update(clock);
  }

  // utility function
  double CalculateWorkingFraction(const double h, const double safety_height) const;
};

static_assert(std::is_trivial_v<DerivedInfo>, "type is not trivial");
