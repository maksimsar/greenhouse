import unittest

from model import Greenhouse


class GreenhouseTests(unittest.TestCase):
    def test_wokwi_state_drives_physical_model(self):
        open_house, closed_house = Greenhouse(), Greenhouse()
        for house in (open_house, closed_house):
            house.scenario("heat")
        for _ in range(8):
            open_house.tick((90, 200, 0, 0))
            closed_house.tick((0, 0, 0, 0))
        self.assertLess(open_house.air, closed_house.air)
        self.assertLess(open_house.humidity, closed_house.humidity)
        self.assertLess(open_house.inside_light, closed_house.inside_light)

    def test_pump_and_missing_measurement(self):
        house = Greenhouse()
        house.scenario("dry")
        old = (house.soil, house.water)
        house.tick((0, 0, 1, 1))
        self.assertGreater(house.soil, old[0])
        self.assertLess(house.water, old[1])
        house.scenario("fault")
        self.assertIn(b",NA,", house.frame())
        self.assertEqual(len(house.frame().strip().split(b",")), 7)

    def test_condensation(self):
        house = Greenhouse()
        house.scenario("morning")
        self.assertTrue(house.condensation)
        for _ in range(20):
            house.tick((90, 0, 0, 0))
        self.assertFalse(house.condensation)


if __name__ == "__main__":
    unittest.main()
