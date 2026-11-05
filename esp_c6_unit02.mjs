// Convertisseur externe Zigbee2MQTT (ESM) pour ESP-C6-UNIT02.
//   Endpoint 10 : lumière couleur (R, G, B, W) -> "rgbw"
//   Endpoint 11 : lumière à gradation (rondelles) -> "rondelles"
// Placer ce fichier dans data/external_converters/, redémarrer Zigbee2MQTT,
// puis refaire l'interview de l'appareil.
// Les noms d'extensions (modernExtend) sont à vérifier contre la version de
// Zigbee2MQTT installée.
import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['ESP-C6-UNIT02'],
    model: 'ESP-C6-UNIT02',
    vendor: 'NathanSensors',
    description: 'Contrôleur RGBW (4 canaux) + sortie rondelles, routeur Zigbee',
    extend: [
        m.deviceEndpoints({endpoints: {rgbw: 10, rondelles: 11}}),
        m.light({
            endpointNames: ['rgbw'],
            color: {modes: ['xy', 'hs'], enhancedHue: false},
            powerOnBehavior: true,
        }),
        m.light({
            endpointNames: ['rondelles'],
            powerOnBehavior: true,
        }),
    ],
    ota: true,
};
