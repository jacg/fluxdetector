#include "my.hh"
#include "materials.hh"

#include <n4-main.hh>
#include <n4-material.hh>
#include <n4-shape.hh>
#include <n4-inspect.hh>
#include <n4-random.hh>

#include <G4Step.hh>
#include <G4EmStandardPhysics_option4.hh>
#include <G4LogicalBorderSurface.hh>
#include <G4OpticalPhysics.hh>
#include <G4OpticalSurface.hh>
#include <G4ParticleDefinition.hh>
#include <G4ParticleGun.hh>
#include <G4PrimaryParticle.hh>
#include <G4String.hh>
#include <G4SystemOfUnits.hh>   // physical units such as `m` for metre
#include <G4Event.hh>           // needed to inject primary particles into an event
#include <G4Box.hh>             // for creating shapes in the geometry
#include <G4Sphere.hh>          // for creating shapes in the geometry
#include <FTFP_BERT.hh>         // our choice of physics list
#include <G4RandomDirection.hh> // for launching particles in random directions

#include <G4ThreeVector.hh>
#include <Randomize.hh>
#include <cstdlib>

std::array <G4double,7> PMTEnergy;
std::array <G4int,7> PMTCount;

n4::sensitive_detector* sensitive_detector(const my& my) {
  auto photon = n4::find_particle("opticalphoton");
  // `process_hits` is a mandatory method of `sensitive_detector`
  auto process_hits = [&my, photon](G4Step* step) {
    auto track = step -> GetTrack();
    track -> SetTrackStatus(fStopAndKill);

    auto pre      = step -> GetPreStepPoint();
    auto copy_nb  = pre  -> GetTouchable() -> GetCopyNumber();
    auto time     = pre  -> GetGlobalTime();
    auto momentum = pre  -> GetMomentum();
    auto energy   = momentum.mag();
    auto particle = track -> GetParticleDefinition();

    if (particle == photon) {
      PMTEnergy[copy_nb] += pre -> GetTotalEnergy(); 
      PMTCount[copy_nb]++;
    }

    return true; // see https://github.com/jacg/nain4/issues/38
  };

  // Optional methods of `sensitive_detector`
  auto init = [&my] (G4HCofThisEvent*) {
    for (auto& e:PMTEnergy){e=0;}
    for (auto& f:PMTCount) {f=0;}
  };
  auto end  = [&my] (G4HCofThisEvent*) {
    for (auto& e:PMTEnergy) {std::cout << e/eV << ", ";};
    for (auto& f:PMTCount)  {std::cout << f    << ", ";}; 
    std::cout << std::endl;
  };

  return (new n4::sensitive_detector{"PMT", process_hits}) // `process_hits` must be given
    -> initialize  (init)                                  // `initialize`   may be skipped
    -> end_of_event(end);                                  // `end_of_event` may be skipped
}

auto PMT(const my& my) {
  static auto dummy_material = air_with_properties();
  static auto unnnumbered_pmt = n4::tubs("PMT")
    .z(my.pmt_thickness).r(my.pmt_radius)
    .sensitive(sensitive_detector(my))
    .place(dummy_material)
    .at_z((my.pmt_thickness-my.detector_length)/2);
  return unnnumbered_pmt;
}

auto my_generator(my& my) {
  my.gun.reset(new G4ParticleGun{n4::find_particle(my.particle)});

  return [&my](G4Event *event) {
    auto random_position_in_detector = [&my] {
      auto [x, z] = n4::random::random_on_disc(my.detector_radius);
      auto y      = n4::random::uniform(
        -my.detector_length / 2,
         my.detector_length / 2
      );
      return G4ThreeVector{x, y, z};
    };
    my.gun -> SetParticlePosition(random_position_in_detector());
    my.gun -> SetParticleEnergy(my.particle_energy);
    my.gun -> SetParticleMomentumDirection(G4RandomDirection());
    my.gun -> GeneratePrimaryVertex(event);
  };
}

n4::actions* create_actions(my& my, unsigned& n_event) {

  auto my_stepping_action = [&] (const G4Step* step) {
    auto pt = step -> GetPreStepPoint();
    auto volume_name = pt -> GetTouchable() -> GetVolume() -> GetName();
    if (volume_name == "Detector") {
      auto pos = pt -> GetPosition();
      auto p = step -> GetTrack() -> GetParticleDefinition() -> GetParticleName();
      std::cout << "pos: " << pos << "   " << p << std::endl;
    }
  };

  auto my_event_action = [&] (const G4Event*) {
     n_event++;
     std::cout << "end of event " << n_event << std::endl;
  };

  return (new n4::        actions{my_generator(my)  })
 -> set( (new n4::   event_action{                  }) -> end(my_event_action) )
 -> set(  new n4::stepping_action{my_stepping_action});
}

auto my_geometry(const my& my) {
  // Heavy water

  auto D2O = d2o_csi_hybrid_FIXME_with_properties(my.scint_yield);

  auto air    = air_with_properties();
  auto Al     = n4::material("G4_Al");
  auto teflon = teflon_with_properties();
  auto world  = n4::box("World").cube(my.lab_size).volume(air);

  auto vessel = n4::tubs("Vessel")
    .r(my.detector_radius + my.vessel_thickness + my.teflon_thickness)
    .z(my.detector_length + my.vessel_thickness + my.teflon_thickness)
    .place(Al)
    .in(world).at_z((my.vessel_thickness+my.teflon_thickness)/2).rotate_x(90*deg).now();

  auto reflector = n4::tubs("Reflector")
    .r(my.detector_radius + my.teflon_thickness)
    .z(my.detector_length + my.teflon_thickness)
    .place(teflon)
    .in(vessel).at_z(-my.vessel_thickness/2).now();

  auto water = n4::tubs("Water")
    .r(my.detector_radius)
    .z(my.detector_length)
    .place(D2O)
    .in(reflector).at_z(-my.teflon_thickness/2).now();

  auto one_pmt = PMT(my).in(water);

  n4::place(one_pmt).copy_no(0).now();
  for (int i=1; i<7; i++) {
    auto margin = my.pmt_radius / 20;
    auto theta = CLHEP::pi * i / 3;
    auto r = my.detector_radius - my.pmt_radius - margin;
    n4::place(one_pmt).copy_no(i).at_x(r * cos(theta)).at_y(r * sin(theta)).now();
  }

  return n4::place(world).now();
}

auto my_physics_list(const my& my) {
  auto physics_list =             new FTFP_BERT                  {my.physics_verbose};
  physics_list ->  ReplacePhysics(new G4EmStandardPhysics_option4{my.em_verbose});
  physics_list -> RegisterPhysics(new G4OpticalPhysics           {my.optical_verbose});
  return physics_list;
}

int main(int argc, char* argv[]) {
  unsigned n_event = 0;

  my my;

  n4::run_manager::create()
    .ui("fluxdetector", argc, argv)
    .macro_path("macs")
    //.apply_command("/my/lab_size 1.2 m")
    //.apply_early_macro("early-hard-wired.mac")
    .apply_cli_early() // CLI --early executed at this point
    // .apply_command(...) // also possible after apply_early_macro

    .physics([&] { return my_physics_list(my); })
    .geometry([&] { return my_geometry(my); })
    .actions(create_actions(my, n_event))

    //.apply_command("/my/particle e-")
    //.apply_late_macro("late-hard-wired.mac")
    .apply_cli_late() // CLI --late executed at this point
    // .apply_command(...) // also possible after apply_late_macro

    .run();
}
