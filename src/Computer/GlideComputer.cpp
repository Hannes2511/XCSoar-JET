// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideComputer.hpp"
#include "Computer/Settings.hpp"
#include "NMEA/Derived.hpp"
#include "GlideComputerInterface.hpp"
#include "Engine/Waypoint/Waypoints.hpp"

using namespace std::chrono;

static PeriodClock last_team_code_update;

GlideComputer::GlideComputer(const ComputerSettings &_settings,
                             const Waypoints &_way_points,
                             Airspaces &_airspace_database,
                             ProtectedTaskManager &task,
                             GlideComputerTaskEvents& events)
  :air_data_computer(_way_points),
   warning_computer(_settings.airspace.warnings, _airspace_database),
   task_computer(task, _airspace_database, &warning_computer.GetManager()),
   idle_condition_monitors(warning_computer.GetManager()),
   waypoints(_way_points),
   retrospective(_way_points),
   team_code_ref_id(-1)
{
  ReadComputerSettings(_settings);
  events.SetComputer(*this);
  idle_clock.Update();
}

void
GlideComputer::ResetFlight(const bool full)
{
  GlideComputerBlackboard::ResetFlight(full);
  air_data_computer.ResetFlight(SetCalculated(), full);
  task_computer.ResetFlight(full);
  stats_computer.ResetFlight(full);
  log_computer.Reset();
  retrospective.Reset();

  cu_computer.Reset();
  warning_computer.Reset();

  trace_history_time.Reset();
}

void
GlideComputer::Initialise()
{
  ResetFlight(true);
}

bool
GlideComputer::ProcessGPS(bool force)
{
  const MoreData &basic = Basic();
  DerivedInfo &calculated = SetCalculated();
  const ComputerSettings &settings = GetComputerSettings();

  const bool last_flying = calculated.flight.flying;

  if (basic.time_available) {
    /* use UTC offset to calculate local time */
    const auto utc_offset = settings.utc_offset.ToDuration();

    calculated.date_time_local = basic.date_time_utc.IsDatePlausible()
      /* known date: apply UTC offset to BrokenDateTime, which may
         increment/decrement date */
      ? basic.date_time_utc + utc_offset
      /* unknown date: apply UTC offset only to BrokenTime, leave the
         BrokenDate part invalid as it was */
      : BrokenDateTime(BrokenDate::Invalid(),
                       basic.date_time_utc.GetTime() + utc_offset);
  } else
    calculated.date_time_local = BrokenDateTime::Invalid();

  calculated.Expire(basic.clock);

  // Process basic information
  air_data_computer.ProcessBasic(Basic(), SetCalculated(),
                                 settings);

  // Process basic task information
  const bool last_finished = calculated.ordered_task_stats.task_finished;

  task_computer.ProcessBasicTask(basic,
                                 calculated,
                                 settings,
                                 force);

  CalculateWorkingBand();

  task_computer.ProcessMoreTask(basic, calculated, settings);

  if (!last_finished && calculated.ordered_task_stats.task_finished)
    OnFinishTask();

  // Check if everything is okay with the gps time and process it
  air_data_computer.FlightTimes(Basic(), SetCalculated(),
                                settings);

  TakeoffLanding(last_flying);

  task_computer.ProcessAutoTask(basic, calculated);

  // Process extended information
  air_data_computer.ProcessVertical(Basic(),
                                    SetCalculated(),
                                    settings);

  stats_computer.ProcessClimbEvents(calculated);

  cu_computer.Compute(basic, calculated, settings);

  // Calculate the team code
  CalculateOwnTeamCode();

  // Calculate the bearing and range of the teammate
  CalculateTeammateBearingRange();

  // update basic trace history
  if (basic.time_available) {
    const auto dt = trace_history_time.Update(basic.time,
                                              milliseconds{500}, seconds{30});
    if (dt.count() > 0)
      calculated.trace_history.append(basic);
    else if (dt.count() < 0)
      /* time warp */
      calculated.trace_history.clear();
  }

  CalculateVarioScale();

  // Update the ConditionMonitors
  condition_monitors.Update(Basic(), Calculated(), settings);

  CalculateFuelBurnTimeRemain(calculated);

  return idle_clock.CheckUpdate(milliseconds(500));
}

void
GlideComputer::CalculateFuelBurnTimeRemain(DerivedInfo &calculated)
{
  const auto &plane = GetComputerSettings().plane;

  if (plane.fuel_consumption < 0 || fabs(plane.fuel_consumption) <= std::numeric_limits<double>::epsilon()) {
    return;
  }

  calculated.fuel_burn_time_remain = plane.fuel_onboard / plane.fuel_consumption * 60.0 * 60.0;
  calculated.fuel_burn_time_remain_available.Update(Basic().clock);
}

void
GlideComputer::ProcessIdle(bool exhaustive)
{
  const MoreData &basic = Basic();
  DerivedInfo &calculated = SetCalculated();

  // Log GPS fixes for internal usage
  // (snail trail, stats, contest, ...)
  stats_computer.DoLogging(basic, calculated);
  log_computer.Run(basic, calculated, GetComputerSettings().logger);

  task_computer.ProcessIdle(basic, calculated, GetComputerSettings(),
                            exhaustive);

  warning_computer.Update(GetComputerSettings(), basic,
                          calculated, calculated.airspace_warnings);

  idle_condition_monitors.Update(basic, calculated, GetComputerSettings());

  // Calculate summary of flight
  if (basic.location_available)
    retrospective.UpdateSample(basic.location);
}

bool
GlideComputer::DetermineTeamCodeRefLocation()
{
  const TeamCodeSettings &settings = GetComputerSettings().team_code;

  if (settings.team_code_reference_waypoint < 0)
    return false;

  if (settings.team_code_reference_waypoint == team_code_ref_id)
    return team_code_ref_found;

  team_code_ref_id = settings.team_code_reference_waypoint;
  const auto wp = waypoints.LookupId(team_code_ref_id);
  if (wp == NULL)
    return team_code_ref_found = false;

  team_code_ref_location = wp->location;
  return team_code_ref_found = true;
}

inline void
GlideComputer::CalculateOwnTeamCode()
{
  // No reference waypoint for teamcode calculation chosen -> cancel
  if (!DetermineTeamCodeRefLocation())
    return;

  // Only calculate every 10sec otherwise cancel calculation
  if (!last_team_code_update.CheckUpdate(seconds(10)))
    return;

  // Get bearing and distance to the reference waypoint
  const GeoVector v = team_code_ref_location.DistanceBearing(Basic().location);

  // Save teamcode to Calculated
  SetCalculated().own_teammate_code.Update(v.bearing, v.distance);
}

static void
ComputeFlarmTeam(const GeoPoint &location, const GeoPoint &reference_location,
                 const TrafficList &traffic_list, const FlarmId target_id,
                 TeamInfo &teamcode_info)
{
  const FlarmTraffic *traffic = traffic_list.FindTraffic(target_id);
  if (traffic == NULL || !traffic->location_available) {
    teamcode_info.flarm_teammate_code_current = false;
    return;
  }

  // Set Teammate location to FLARM contact location
  teamcode_info.teammate_location = traffic->location;
  teamcode_info.teammate_vector = location.DistanceBearing(traffic->location);
  teamcode_info.teammate_available = true;

  // Calculate distance and bearing from teammate to reference waypoint

  GeoVector v = reference_location.DistanceBearing(traffic->location);

  // Calculate TeamCode and save it in Calculated
  teamcode_info.flarm_teammate_code.Update(v.bearing, v.distance);
  teamcode_info.flarm_teammate_code_current = true;
}

static void
ComputeTeamCode(const GeoPoint &location, const GeoPoint &reference_location,
                const TeamCode &team_code,
                TeamInfo &teamcode_info)
{
  // Calculate bearing and distance to teammate
  teamcode_info.teammate_location = team_code.GetLocation(reference_location);
  teamcode_info.teammate_vector =
    location.DistanceBearing(teamcode_info.teammate_location);
  teamcode_info.teammate_available = true;
}

void
GlideComputer::CalculateTeammateBearingRange()
{
  const TeamCodeSettings &settings = GetComputerSettings().team_code;
  const NMEAInfo &basic = Basic();
  TeamInfo &teamcode_info = SetCalculated();

  // No reference waypoint for teamcode calculation chosen -> cancel
  if (!DetermineTeamCodeRefLocation())
    return;

  if (settings.team_flarm_id.IsDefined()) {
    ComputeFlarmTeam(basic.location, team_code_ref_location,
                     basic.flarm.traffic, settings.team_flarm_id,
                     teamcode_info);
  } else if (settings.team_code.IsDefined()) {
    teamcode_info.flarm_teammate_code.Clear();

    ComputeTeamCode(basic.location, team_code_ref_location,
                    settings.team_code,
                    teamcode_info);
  } else {
    teamcode_info.teammate_available = false;
    teamcode_info.flarm_teammate_code.Clear();
  }
}

inline void
GlideComputer::OnTakeoff()
{
  // reset stats on takeoff
  air_data_computer.ResetFlight(SetCalculated(), false);

  // save stats in case we never finish
  SaveFinish();
}

inline void
GlideComputer::OnLanding()
{
  // JMWX  restore data calculated at finish so
  // user can review flight as at finish line

  if (Calculated().ordered_task_stats.task_finished)
    RestoreFinish();
}

inline void
GlideComputer::TakeoffLanding(bool last_flying)
{
  if (Calculated().flight.flying && !last_flying)
    OnTakeoff();
  else if (!Calculated().flight.flying && last_flying)
    OnLanding();
}

void
GlideComputer::OnStartTask()
{
  GlideComputerBlackboard::StartTask();
  air_data_computer.ResetStats();
  stats_computer.StartTask(Basic());
  log_computer.StartTask(Basic());
}

void
GlideComputer::OnFinishTask()
{
  SaveFinish();
}

void
GlideComputer::OnTransitionEnter()
{
  log_computer.SetFastLogging();
}


void
GlideComputer::SetTerrain(RasterTerrain* _terrain)
{
  air_data_computer.SetTerrain(_terrain);
  task_computer.SetTerrain(_terrain);
}

void
GlideComputer::CalculateWorkingBand()
{
  const MoreData &basic = Basic();
  DerivedInfo &calculated = SetCalculated();
  const ComputerSettings &settings = GetComputerSettings();

  calculated.common_stats.height_min_working = stats_computer.GetFlightStats().GetMinWorkingHeight();
  if (calculated.terrain_base_valid) {
    calculated.common_stats.height_min_working = std::max(calculated.common_stats.height_min_working,
                                                          calculated.GetTerrainBaseFallback()+settings.task.safety_height_arrival);
  }
  calculated.common_stats.height_max_working = std::max(calculated.common_stats.height_min_working,
                                                        stats_computer.GetFlightStats().GetMaxWorkingHeight());

  calculated.common_stats.height_fraction_working = 1; // fallback;

  if (basic.NavAltitudeAvailable()) {
    calculated.common_stats.height_max_working = std::max(calculated.common_stats.height_max_working,
                                                          basic.nav_altitude);
    calculated.common_stats.height_fraction_working =
        calculated.CalculateWorkingFraction(basic.nav_altitude,
                                            settings.task.safety_height_arrival);
  }
}

void
GlideComputer::CalculateVarioScale()
{
  DerivedInfo &calculated = SetCalculated();
  const GlidePolar &glide_polar = GetComputerSettings().polar.glide_polar_task;
  calculated.common_stats.vario_scale_positive =
      std::max(stats_computer.GetFlightStats().GetVarioScalePositive(),
               glide_polar.GetMC());
  calculated.common_stats.vario_scale_negative =
      std::min(stats_computer.GetFlightStats().GetVarioScaleNegative(),
               -glide_polar.GetSBestLD());
}
