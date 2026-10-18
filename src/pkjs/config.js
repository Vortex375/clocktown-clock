// Clay settings page. Select values are sent as strings ("0".."4").
var corner = function (key, label, def) {
  return {
    type: 'select',
    messageKey: key,
    label: label,
    defaultValue: def,
    options: [
      { label: 'Nothing', value: '0' },
      { label: 'Weather', value: '1' },
      { label: 'Date', value: '2' },
      { label: 'Steps', value: '3' },
      { label: 'Heart rate', value: '4' }
    ]
  };
};

module.exports = [
  { type: 'heading', defaultValue: 'Clock Town Clock' },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Time' },
      {
        type: 'select',
        messageKey: 'TimePosition',
        label: 'Digital time',
        defaultValue: '3',
        options: [
          { label: 'Centre of the clock', value: '0' },
          { label: 'Bottom of the screen', value: '1' },
          { label: 'On the ring and disc', value: '3' },
          { label: 'Hidden', value: '2' }
        ]
      },
      { type: 'toggle', messageKey: 'Use12h', label: 'Use 12-hour format', defaultValue: false }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Corners' },
      corner('CornerTL', 'Top left', '3'),
      corner('CornerTR', 'Top right', '1'),
      corner('CornerBL', 'Bottom left', '4'),
      corner('CornerBR', 'Bottom right', '2')
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Colours' },
      {
        type: 'color',
        messageKey: 'BackgroundColor',
        label: 'Background',
        defaultValue: '0x550000',
        sunlight: false
      }
    ]
  },
  { type: 'submit', defaultValue: 'Save' }
];
